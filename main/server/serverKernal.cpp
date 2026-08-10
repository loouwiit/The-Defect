#include <time.h>

#include <esp_task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <esp_core_dump.h>
#include <esp_log.h>
#include <lwip/sockets.h>

#include <cstdlib>
#include <cstring>

#include "utility/stringCompare.hpp"

#include "serverKernal.hpp"
#include "wifi/http.hpp"
#include "wifi/socketStreamWindow.hpp"
#include "wifi/wifi.hpp"
#include "wifi/mdns.hpp"

#include "buildinHtml/file.hpp"
#include "storage/fat.hpp"
#include "aiAgent/aiConfig.hpp"
#include "aiAgent/aiClient.hpp"
#include "aiAgent/aiParser.hpp"
#include "ota/otaManager.hpp"
#include "logService/logService.hpp"

#include "task/task.hpp"

EXT_RAM_BSS_ATTR static uint8_t autoRestartTimes = 0;
EXT_RAM_BSS_ATTR static uint8_t maxRestartTimes = 0;

constexpr char TAG[] = "server";

constexpr char ServerPath[] = "/server";
constexpr char FormatingPassword[] = "I know exactly what I'm doing";
constexpr size_t FlashPutMaxSize = 10 * 1024 * 1024; //10M
constexpr size_t MemPutMaxSize = 10 * 1024 * 1024; //10M
constexpr size_t SdPutMaxSize = 50 * 1024 * 1024; //50M
constexpr size_t PutBufferSize = 512;

// ⚠ POST API 合理 body 上限（防伪造超大 Content-Length → new 失败崩溃；OTA 流式上传单独放行）
constexpr size_t kMaxPostBody = 64 * 1024;
// ⚠ DELETE body 是路径，上限 512
constexpr size_t kMaxDeletePath = 512;

constexpr size_t SocketStreamWindowNumber = 16;
constexpr size_t CoworkerNumber = 6;

constexpr size_t WifiApRecordSize = 50;

EXT_RAM_BSS_ATTR Socket serverListenSocket{};
EXT_RAM_BSS_ATTR bool serverRunning = false;
EXT_RAM_BSS_ATTR SocketStreamWindow socketStreamWindows[SocketStreamWindowNumber]{};
EXT_RAM_BSS_ATTR static Mutex scanMutex{};
EXT_RAM_BSS_ATTR static size_t scanPosition = 0;

/* /api/coredump 下载用：分块读 coredump 分区（读 flash 禁 cache 窗口内访问要求内部 RAM）。
 * 普通 static（不加 EXT_RAM_BSS_ATTR）→ .bss 内部 RAM；经 runInInternalStack 串行使用。
 * 16KB：512KB 分区仅 32 次调用（每次调用会产生 2 条 otaInt 日志，块越大噪音越少）。 */
static uint8_t s_coredumpChunk[16 * 1024] __attribute__((aligned(4)));

EXT_RAM_BSS_ATTR TaskHandle_t serverHandle{};
EXT_RAM_BSS_ATTR StackType_t* serverStack{};
StaticTask_t serverTask{};

EXT_RAM_BSS_ATTR TaskHandle_t coWorkerHandle[CoworkerNumber]{};
EXT_RAM_BSS_ATTR StackType_t* coWorkerStack[CoworkerNumber]{};
StaticTask_t coWorkerTasks[CoworkerNumber]{};

void server(void*);
void serverCoworker(void*);
void recieve(IOSocketStream& socketStream);
void sendOk(OSocketStream& socketStream);
void sendBadRequest(OSocketStream& socketStream);
void httpGet(IOSocketStream& socketStream, HttpRequest& request);
void httpPost(IOSocketStream& socketStream, HttpRequest& request);
void httpOta(IOSocketStream& socketStream, HttpRequest& request);
static void httpOtaInternal(void* p);
void httpOtaSetBoot(IOSocketStream& socketStream, HttpRequest& request);
void httpOtaConfirm(IOSocketStream& socketStream, HttpRequest& request);
void httpPut(IOSocketStream& socketStream, HttpRequest& request);
void httpDelete(IOSocketStream& socketStream, HttpRequest& request);
void restart();

void apiFloor(OSocketStream& socketStream, const char* path);

bool serverIsStarted()
{
	return serverRunning;
}

void serverStart(unsigned char maxAutoRestartTimes)
{
	autoRestartTimes = 0;
	maxRestartTimes = maxAutoRestartTimes;
	if (serverRunning) return;
	serverRunning = true;

	if (serverStack == nullptr)
		serverStack = new StackType_t[4096];
	serverHandle = xTaskCreateStatic(server, "server", 4096, nullptr, Task::Priority::Deamon, serverStack, &serverTask);
	if (serverHandle == nullptr)
	{
		delete[] serverStack;
		serverStack = nullptr;
		serverRunning = false;
		return;
	}

	char serverCoTaskName[13] = "coServer00";
	size_t startedCoTask = 0;
	for (size_t i = 0; i < CoworkerNumber; i++)
	{
		if (coWorkerStack[i] != nullptr) continue;
		coWorkerStack[i] = new StackType_t[4096];
		sprintf(serverCoTaskName, "coServer%02d", i);
		coWorkerHandle[i] = xTaskCreateStatic(serverCoworker, serverCoTaskName, 4096, (void*)i, Task::Priority::Deamon, coWorkerStack[i], &coWorkerTasks[i]);
		if (coWorkerHandle[i] != nullptr)
			startedCoTask++;
		else
		{
			delete[] coWorkerStack[i];
			coWorkerStack[i] = nullptr;
		}
	}
	if (startedCoTask == 0)
		serverStop();
}

void serverStop()
{
	serverRunning = false;
	close(serverListenSocket);
}

void server(void*)
{
	serverListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (serverListenSocket < 0)
	{
		ESP_LOGE(TAG, "socket creat failed: error %d", serverListenSocket);
		serverRunning = false;
		vTaskDelete(nullptr);
	}

	int opt = 1;
	setsockopt(serverListenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	ESP_LOGI(TAG, "socket created");

	sockaddr_storage addr = {};
	{
		struct sockaddr_in* dest_addr_ip4 = (struct sockaddr_in*)&addr;
		dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
		dest_addr_ip4->sin_family = AF_INET;
		dest_addr_ip4->sin_port = htons(80);
	}

	int err;
	err = bind(serverListenSocket, (struct sockaddr*)&addr, sizeof(addr));
	if (err != 0)
	{
		ESP_LOGE(TAG, "socket bind failed: errno %d", errno);
		serverRunning = false;
		vTaskDelete(nullptr);
	}
	ESP_LOGI(TAG, "socket bound, port 80");

	err = listen(serverListenSocket, 1);
	if (err != 0)
	{
		ESP_LOGE(TAG, "socket listen failed: errno %d", errno);
		serverRunning = false;
		vTaskDelete(nullptr);
	}

	while (serverRunning)
	{
		ESP_LOGD(TAG, "socket listening");

		struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
		socklen_t addr_len = sizeof(source_addr);
		int sock = accept(serverListenSocket, (struct sockaddr*)&source_addr, &addr_len);
		if (sock < 0)
		{
			ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
			restart();
			break;
		}

		// Set tcp keepalive option
		int keepAlive = 1;
		int keepIdle = 5;
		int keepInterval = 5;
		int keepCount = 3;
		setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
		setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
		setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
		setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

		// Convert ip address to string
		char addr_str[128] = "unknown";   // ⚠ 初始化默认值：IPv6 分支不写 → 避免打印未初始化栈内存
		if (source_addr.ss_family == PF_INET)
		{
			inet_ntoa_r(((sockaddr_in*)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
		}

		ESP_LOGD(TAG, "socket accepted ip address: %s", addr_str);

		// accept 成功 → 重置重启计数（否则多次失败累计 3 次永久停服）
		autoRestartTimes = 0;

		size_t index = 0;
		while (!socketStreamWindows[index].setSocket(sock))
		{
			index++;
			if (index >= SocketStreamWindowNumber)
			{
				index = 0;
				ESP_LOGW(TAG, "all window was occupied");
				vTaskDelay(100 / portTICK_PERIOD_MS);
			}
		}
		ESP_LOGD(TAG, "SocketStreamWindow state: %d / %d", SocketStreamWindow::getEnabledCount(), SocketStreamWindowNumber);
		vTaskDelay(1);
	}

	close(serverListenSocket);

	// 如果是restart过来的就不需要释放内存
	if (!serverRunning) for (size_t i = 0; i < CoworkerNumber; i++)
	{
		while (coWorkerHandle[i] != nullptr)
			vTaskDelay(1);

		delete[] coWorkerStack[i];
		coWorkerStack[i] = nullptr;

		Task::addTask([](void*)->TickType_t
			{
				delete[] serverStack;
				serverStack = nullptr;
				return Task::infinityTime;
			}, "deleting server", nullptr, 1000);
	}

	vTaskDelete(nullptr);
}

void serverCoworker(void* coworkerIndex)
{
	bool scanning = false;
	size_t index = 0;

	ESP_LOGI(TAG, "Coworker %d started", (int)coworkerIndex);
	while (serverRunning)
	{
		//尝试获取扫描权
		//printf("coworker%d: waiting for scanning\n", (int)coworkerIndex);
		scanMutex.lock();

		//获取成功，加载起始点
		scanning = true;
		index = scanPosition;
		//printf("coworker%d: scanning from %d\n", (int)coworkerIndex, (int)scanPosition);

		while (scanning && serverRunning)
		{
			for (; index < SocketStreamWindowNumber; index++)
			{

				//尝试激活每一个窗口
				if (!socketStreamWindows[index].enable()) continue;

				//已激活窗口，检查是否需要处理数据
				//printf("coworker%d: checking %d\n", (int)coworkerIndex, (int)index);
				if (socketStreamWindows[index].check())
				{
					//移交扫描权
					scanPosition = (index + 1) % SocketStreamWindowNumber;
					scanMutex.unlock();

					//处理数据
					//printf("coworker%d: working on window %d\n", (int)coworkerIndex, index);
					recieve(socketStreamWindows[index].getSocketStream());
					socketStreamWindows[index].disable(); //窗口已激活，现取消激活窗口

					//取消自身扫描
					index = SocketStreamWindowNumber;
					scanning = false;
				}
				else
				{
					socketStreamWindows[index].disable(); //窗口已激活，现取消激活窗口
				}
			}
			index = 0;
			vTaskDelay(1); //一周下来都没有任务或刚处理完一个请求
		}

		// scanning && serverRunning不成立
		if (scanning)
		{
			// scanning == true, serverRunning == false
			// server关闭，移交扫描权，使其他线程发现关闭
			scanning = false;
			scanMutex.unlock();
		}
		// scanning == false，无需移交扫描权
	}
	ESP_LOGI(TAG, "Coworker %d ended", (int)coworkerIndex);
	coWorkerHandle[(int)coworkerIndex] = nullptr;
	vTaskDelete(NULL);
}

void recieve(IOSocketStream& socketStream)
{
	HttpRequest request;
	request.receive(socketStream);

	// print request infomation
	{
		ESP_LOGD(TAG, "recieve: method = %s", getMethodNameFromMethod(request.getMethod()));
		ESP_LOGD(TAG, "recieve: path = %s", request.getPath());

		HttpStringPairSize_t headNumber = request.heads.getNumber();
		for (HttpStringPairSize_t i = 0; i < headNumber; i++)
		{
			const char* name = request.heads[i].getName();
			const char* value = request.heads[i].getValue();
			ESP_LOGD(TAG, "recieve: head[%d] is %s : %s", i, name, value);
		}

		HttpStringPairSize_t cookieNumber = request.cookies.getNumber();
		for (HttpStringPairSize_t i = 0; i < cookieNumber; i++)
		{
			ESP_LOGD(TAG, "recieve: cookie[%d] is %s = %s", i, request.cookies[i].getName(), request.cookies[i].getValue());
		}

		ESP_LOGD(TAG, "recieve: body lenght = %u", request.getBodyLenght());
	}

	switch (request.getMethod())
	{
	default:
	case HttpMethod::Get:
		httpGet(socketStream, request);
		break;
	case HttpMethod::Post:
		httpPost(socketStream, request);
		break;
	case HttpMethod::Put:
		httpPut(socketStream, request);
		break;
	case HttpMethod::Delete:
		httpDelete(socketStream, request);
		break;
	}
	socketStream.sendNow();

	if (!socketStream.isGood())
		ESP_LOGW(TAG, "recieve: socketStream error");
}

void sendOk(OSocketStream& socketStream)
{
	HttpRespond respond;

	respond.cookies.clear();

	respond.heads.clear();
	// ⚠ 必须设置 Content-Type：缺失时浏览器按 text/xml 解析（Firefox 报 XML 解析错误）
	respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });

	respond.setBody((void*)HttpReason::Ok);
	respond.setBodyLenght(sizeof(HttpReason::Ok) - 1);
	respond.setStatus(HttpStatus::Ok);
	respond.setReason(HttpReason::Ok);
	respond.send(socketStream);
}

void sendBadRequest(OSocketStream& socketStream)
{
	HttpRespond respond;

	respond.cookies.clear();

	respond.heads.clear();
	respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });

	respond.setStatus(HttpStatus::BadRequest);
	respond.setReason(HttpReason::BadRequest);
	respond.setBody((void*)HttpReason::BadRequest);
	respond.setBodyLenght(sizeof(HttpReason::BadRequest) - 1);
	respond.send(socketStream);
}

/* v5.5.2 无 esp_reset_reason_str()，用数字映射（枚举值见 esp_system.h esp_reset_reason_t） */
static const char* resetReasonName(int reason)
{
	static const char* const names[] = {
		"ESP_RST_UNKNOWN",    /* 0 */
		"ESP_RST_POWERON",    /* 1 */
		"ESP_RST_EXT",        /* 2 */
		"ESP_RST_SW",         /* 3 */
		"ESP_RST_PANIC",      /* 4 */
		"ESP_RST_INT_WDT",    /* 5 */
		"ESP_RST_TASK_WDT",   /* 6 */
		"ESP_RST_WDT",        /* 7 */
		"ESP_RST_DEEPSLEEP",  /* 8 */
		"ESP_RST_BROWNOUT",   /* 9 */
		"ESP_RST_SDIO",       /* 10 */
		"ESP_RST_USB",        /* 11 */
		"ESP_RST_JTAG",       /* 12 */
		"ESP_RST_EFUSE",      /* 13 */
		"ESP_RST_PWR_GLITCH", /* 14 */
		"ESP_RST_CPU_LOCKUP", /* 15 */
	};
	if (reason < 0 || reason > 15) return "UNKNOWN";
	return names[reason];
}

// 循环读取完整 body。readByte 非阻塞（IBuffer 仅 64B），单次可能读不满；
// 残留 body 会被同一连接的下一个请求误当请求行（曾出现 path=ota_1",... 诡异 POST）。
// 成功返回 true，并在 buf[len] 写 '\0'（调用方需保证 buf 至少 len+1 字节）。
static bool readBodyFully(IOSocketStream& socketStream, size_t len, char* buf)
{
	size_t got = 0;
	while (got < len)
	{
		size_t n = socketStream.readByte(buf + got, len - got);
		if (n == 0)
		{
			if (!socketStream.isGood()) return false;
			vTaskDelay(1);
			continue;
		}
		got += n;
	}
	buf[len] = '\0';
	return true;
}

void httpGet(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = (!(request.getPath()[0] == '/' && request.getPath()[1] == '\0')) ? (request.getPath()) : ("/index.html");

	// ── API: OTA 状态（槽位/版本/上次回滚）──
	if (stringCompare((char*)uri, strlen(uri), "/api/otaInfo", 12))
	{
		// getInfoJson 内部 mmap flash（禁用 cache 要求栈在内部 RAM），coWorker 栈在 PSRAM →
		// 经 runInInternalStack 在内部 RAM 栈任务中执行（阻塞）。
		char body[1024];
		struct Ctx { char* buf; size_t size; };
		Ctx ctx{ body, sizeof(body) };
		OtaManager::instance().runInInternalStack([](void* p) {
			auto* c = (Ctx*)p;
			OtaManager::instance().getInfoJson(c->buf, c->size);
		}, &ctx);
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " application/json; charset=utf-8" });
		respond.setBody(body);
		respond.setBodyLenght(strlen(body));
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		respond.send(socketStream);
		return;
	}

	// ── API: 读取 AI 配置 ──
	if (stringCompare((char*)uri, strlen(uri), "/api/aiConfig", 13))
	{
		AiConfig cfg = aiConfigLoad();
		char body[512];
		int n = snprintf(body, sizeof(body),
			"{\"api_url\":\"%s\",\"model\":\"%s\",\"timeout_ms\":%d,\"max_tokens\":%d,\"has_key\":%s}",
			cfg.apiUrl.c_str(), cfg.model.c_str(),
			cfg.timeoutMs, cfg.maxTokens,
			cfg.valid ? "true" : "false");
		// ⚠ snprintf 返回值可能 ≥ sizeof(body)（配置超长时），必须截断防越界读
		if (n >= (int)sizeof(body)) n = (int)sizeof(body) - 1;
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " application/json; charset=utf-8" });
		respond.setBody(body);
		respond.setBodyLenght(n);
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		respond.send(socketStream);
		return;
	}

	// ── API: AI 连接测试 ──
	if (stringCompare((char*)uri, strlen(uri), "/api/aiTest", 11))
	{
		AiConfig cfg = aiConfigLoad();
		if (!cfg.valid) {
			HttpRespond respond;
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
			respond.setBody((void*)"❌ API Key 未配置");
			respond.setBodyLenght(21);
			respond.send(socketStream);
			return;
		}
		HttpClient::send(cfg, "Respond with exactly: OK", "test",
			[](bool ok, const std::string& resp, const std::string& err) {
				// 异步，无法同步返回结果
			});
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		respond.setBody((void*)"⏳ 测试请求已发送，请检查设备日志");
		respond.setBodyLenght(49);
		respond.send(socketStream);
		return;
	}

	// ── API: 读取 mDNS 配置 ──
	if (stringCompare((char*)uri, strlen(uri), "/api/mdnsConfig", 15))
	{
		const char* hostname = mdnsGetHostname();
		char body[256];
		int n = snprintf(body, sizeof(body),
			"{\"hostname\":\"%s\",\"instance\":\"%s Game Console\"}",
			hostname, hostname);
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " application/json; charset=utf-8" });
		respond.setBody(body);
		respond.setBodyLenght(n);
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		respond.send(socketStream);
		return;
	}

	// ── API: 日志尾部（支持 ?tail=N，默认 200 行）──
	if (strncmp(uri, "/api/log", 8) == 0 && (uri[8] == '\0' || uri[8] == '?'))
	{
		size_t maxLines = 200;
		const char* q = strchr(uri, '?');
		if (q)
		{
			const char* tail = strstr(q, "tail=");
			if (tail)
			{
				int n = atoi(tail + 5);
				if (n > 0 && n <= 2000) maxLines = (size_t)n;
			}
		}
		// ⚠ 栈上 64KB 会爆 worker 栈（16KB）→ 堆分配（PSRAM）
		char* body = new char[64 * 1024];
		LogService::instance().readTail(body, 64 * 1024, maxLines);
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		// 顶层导航（window.open / <a download>）触发文件下载；fetch 历史回填不受影响
		respond.heads.add({ "Content-Disposition", " attachment; filename=\"app.log\"" });
		// 日志是动态文件，禁止缓存（否则浏览器启发式缓存会显示旧内容，误判"日志停滞"）
		respond.heads.add({ "Cache-Control", " no-store" });
		respond.setBody(body);
		respond.setBodyLenght(strlen(body));
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		respond.send(socketStream);
		delete[] body;
		return;
	}

	// ── API: 复位/崩溃信息 ──
	if (stringCompare((char*)uri, strlen(uri), "/api/resetInfo", 14))
	{
		LogService& log = LogService::instance();
		esp_reset_reason_t reason = esp_reset_reason();
		const char* reasonName = resetReasonName((int)reason);
		int logLevel = log.getGlobalLevel();
		if (logLevel < 0) logLevel = 3;   // 未设置 → INFO

		// 收集按 tag 源头过滤列表（≤8 项），拼入 JSON
		char tagBuf[8][48];
		int tagLv[8];
		size_t tagCount = log.getTagLevelCount();
		if (tagCount > 8) tagCount = 8;
		for (size_t i = 0; i < tagCount; i++)
		{
			int lv = -1;
			if (log.getTagLevelAt(i, tagBuf[i], sizeof(tagBuf[i]), &lv))
				tagLv[i] = lv;
			else
			{
				tagBuf[i][0] = '\0';
				tagLv[i] = -1;
			}
		}

		char body[768];
		int n = snprintf(body, sizeof(body),
			"{\"reset_reason\":%d,\"reset_reason_name\":\"%s\",\"panic_count\":%u,\"last_crash\":%lld,\"has_coredump\":%s,\"log_level\":%d,\"file_level\":%d,\"ws_level\":%d,\"tag_levels\":{",
			(int)reason, reasonName ? reasonName : "unknown",
			(unsigned)log.getPanicCount(), (long long)log.getLastCrashTime(),
			log.hasCoredump() ? "true" : "false",
			logLevel, log.getFileLevel(), log.getWsLevel());
		if (n < (int)sizeof(body))
		{
			for (size_t i = 0; i < tagCount && tagBuf[i][0]; i++)
			{
				int written = snprintf(body + n, sizeof(body) - n, "\"%s\":%d,", tagBuf[i], tagLv[i]);
				if (written < 0 || n + written >= (int)sizeof(body)) break;
				n += written;
			}
			if (n > 0 && body[n - 1] == ',') n--;   // 去掉尾部逗号
			if (n < (int)sizeof(body) - 2)
				n += snprintf(body + n, sizeof(body) - n, "}}");
			else
				n = (int)sizeof(body) - 1;
		}
		else
			n = (int)sizeof(body) - 1;
		// ⚠ snprintf 返回值可能 ≥ sizeof(body)，必须截断防越界读
		if (n >= (int)sizeof(body)) n = (int)sizeof(body) - 1;
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " application/json; charset=utf-8" });
		respond.setBody(body);
		respond.setBodyLenght(n);
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		respond.send(socketStream);
		return;
	}

	// ── API: 下载 coredump 分区（ELF 崩溃转储）──
	if (stringCompare((char*)uri, strlen(uri), "/api/coredump", 13))
	{
		// 读 flash（禁 cache 窗口）→ runInInternalStack（内部 RAM 栈）+ 内部 RAM 块缓冲中转
		if (OtaManager::instance().isUpdating())
		{
			sendBadRequest(socketStream);
			return;
		}
		const esp_partition_t* part = esp_partition_find_first(
			ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
		if (!part)
		{
			sendBadRequest(socketStream);
			return;
		}

		// 分区第一个字 = 转储大小（0xFFFFFFFF=空）
		uint32_t dumpSize = 0xFFFFFFFF;
		{
			struct ReadSizeCtx { const esp_partition_t* part; uint32_t* out; esp_err_t err; };
			ReadSizeCtx ctx{ part, &dumpSize, ESP_FAIL };
			OtaManager::instance().runInInternalStack([](void* p) {
				auto* c = (ReadSizeCtx*)p;
				c->err = esp_partition_read(c->part, 0, c->out, sizeof(uint32_t));
			}, &ctx);
			if (ctx.err != ESP_OK) { sendBadRequest(socketStream); return; }
		}

		if (dumpSize == 0xFFFFFFFF || dumpSize == 0 || dumpSize > part->size)
		{
			HttpRespond respond;
			respond.setStatus(HttpStatus::NotFound);
			respond.setReason(HttpReason::NotFound);
			respond.setBody((void*)HttpReason::NotFound);
			respond.setBodyLenght(sizeof(HttpReason::NotFound) - 1);
			respond.send(socketStream);
			return;
		}

		// 分块读入 PSRAM 缓冲（每块经内部栈任务 + 内部 RAM 缓冲中转）
		size_t total = (size_t)dumpSize;
		char* blob = new char[total];
		size_t off = 0;
		bool ok = true;
		while (off < total && ok)
		{
			size_t chunk = (total - off < sizeof(s_coredumpChunk)) ? (total - off) : sizeof(s_coredumpChunk);
			struct ReadChunkCtx { const esp_partition_t* part; size_t off; uint8_t* buf; size_t len; esp_err_t err; };
			ReadChunkCtx ctx{ part, off, s_coredumpChunk, chunk, ESP_FAIL };
			OtaManager::instance().runInInternalStack([](void* p) {
				auto* c = (ReadChunkCtx*)p;
				c->err = esp_partition_read(c->part, c->off, c->buf, c->len);
			}, &ctx);
			if (ctx.err != ESP_OK) { ok = false; break; }
			memcpy(blob + off, s_coredumpChunk, chunk);
			off += chunk;
		}

		if (!ok)
		{
			delete[] blob;
			sendBadRequest(socketStream);
			return;
		}

		HttpRespond respond;
		respond.heads.add({ "Content-Type", " application/octet-stream" });
		respond.setBody(blob);
		respond.setBodyLenght(total);
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		respond.send(socketStream);
		delete[] blob;
		return;
	}

	HttpContentType contentType = getContentTypeFromPath(uri);
	HttpRespond respond;

	{
		respond.cookies.clear();

		respond.heads.clear();
		if (contentType != HttpContentType::Other)
			respond.heads.add({ "Content-Type", getContentTypeNameFromContentType(contentType) });
	}

	if (stringCompare(uri, strlen(uri), "/file", 5) || stringCompare(uri, strlen(uri), "/File", 5) || stringCompare(uri, strlen(uri), "/FILE", 5))
	{
		//管理页面
		respond.setBody((void*)HtmlFile);
		respond.setBodyLenght(sizeof(HtmlFile) - 1);
		respond.send(socketStream);
	}
	else
	{
		IFile file;
		char* path = nullptr;
		if (prefixCompare(uri, strlen(uri), "/file", 5) || prefixCompare(uri, strlen(uri), "/File", 5) || prefixCompare(uri, strlen(uri), "/FILE", 5))
		{
			//全部文件
			path = new char[sizeof(PerfixRoot) + strlen(uri) - 5];
			sprintf(path, "%s%s", PerfixRoot, uri + 5);
		}
		else
		{
			//转到serverPath下
			path = new char[sizeof(PerfixRoot) + sizeof(ServerPath) + strlen(uri)];
			sprintf(path, "%s%s%s", PerfixRoot, ServerPath, uri);
		}
		file.open(path);
		delete[] path;
		if (file.isOpen())
		{
			respond.setBody(file);
			file.reGetSize();
			respond.setBodyLenght(file.getSize());

			respond.setStatus(HttpStatus::Ok);
			respond.setReason(HttpReason::Ok);
		}
		else
		{
			respond.setBody((void*)HttpReason::NotFound);
			respond.setBodyLenght(sizeof(HttpReason::NotFound) - 1);

			respond.setStatus(HttpStatus::NotFound);
			respond.setReason(HttpReason::NotFound);
		}
		respond.send(socketStream);
		file.close();
	}
}

void httpPost(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	const size_t uriLenght = strlen(uri);

	// ⚠ 伪造超大 Content-Length（如 2GB）→ new 失败 → 无异常处理 → abort 崩溃。
	//   统一上限 64KB（覆盖各 POST API 合理 body）；OTA 上传是流式写入（httpOta），
	//   不分配 body 缓冲，且 body 可达 6MB → 精确匹配 /api/ota（8 字符）单独放行。
	if (!stringCompare((char*)uri, uriLenght, "/api/ota", 8) &&
		request.getBodyLenght() > kMaxPostBody)
	{
		sendBadRequest(socketStream);
		return;
	}

	if (stringCompare((char*)uri, uriLenght, "/api/floor", 10))
	{
		size_t pathSize = sizeof(PerfixRoot) + request.getBodyLenght();
		char* path = new char[pathSize];
		strcpy(path, PerfixRoot);
		if (!readBodyFully(socketStream, request.getBodyLenght(), path + sizeof(PerfixRoot) - 1))
		{
			delete[] path;
			sendBadRequest(socketStream);
			return;
		}
		apiFloor(socketStream, path);
		delete[] path;
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/getSpace", 13))
	{
		uint64_t free = 0;
		uint64_t totol = 0;

		char buffer[41] = "";
		size_t count = 0;

		getSpace(free, totol);

		count = sprintf(buffer, "%llu,%llu", free, totol);

		HttpRespond respond;
		// ⚠ 必须设置 Content-Type：缺失时浏览器按 text/xml 解析（Firefox 报 XML 解析错误）
		respond.cookies.clear();
		respond.heads.clear();
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		respond.setBody(buffer);
		respond.setBodyLenght(count);

		respond.send(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/wifiScan", 13))
	{
		if (!wifiStationIsStarted())
		{
			// 发送错误
			HttpRespond respond;
			respond.setBody((void*)"ERROR:wifi not started");
			respond.setBodyLenght(22);
			respond.send(socketStream);
			return;
		}

		// 扫描
		wifi_ap_record_t* wifi = new wifi_ap_record_t[WifiApRecordSize]; // 96 bytes per record
		size_t count = wifiStationScan(wifi, WifiApRecordSize);
		// ⚠ esp_wifi_scan_get_ap_records 的返回数是实际 AP 数（可能 > 传入上限），
		//   写入缓冲的记录数 ≤ WifiApRecordSize —— 必须截断，否则下方循环越界读
		if (count > WifiApRecordSize) count = WifiApRecordSize;

		// 统计长度
		// rssi 可为 "-100"（4 位含负号），每行按 strlen(ssid) + 6 估算（rssi 4位 + ':' + '\n'）
		size_t bodyLenght = 0;
		for (size_t i = 0;i < count;i++)
		{
			bodyLenght += strlen((char*)wifi[i].ssid) + 6;//rssi的4位，':'的1位，'\n'的1位
		}

		// 构建body
		char* body = new char[bodyLenght + 1];
		size_t pointer = 0;
		for (size_t i = 0;i < count;i++)
		{
			pointer += sprintf(body + pointer, "%d:%s\n", (int)wifi[i].rssi, (char*)wifi[i].ssid);
		}

		// 发送数据
		HttpRespond respond;
		respond.setBody(body);
		respond.setBodyLenght(pointer);
		respond.send(socketStream);

		// 清理内存
		delete[] body;
		body = nullptr;

		delete[] wifi;
		wifi = nullptr;
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/wifiStart", 14))
	{
		if (!wifiStationIsStarted()) wifiStationStart();

		if (request.bodyLenght != 0)
		{
			// body 格式: ssid\npassword\n（循环读完，防残留）
			char* body = new char[request.bodyLenght + 1];
			if (!readBodyFully(socketStream, request.bodyLenght, body))
			{
				delete[] body;
				sendBadRequest(socketStream);
				return;
			}
			char* nl = strchr(body, '\n');
			if (nl) *nl = '\0';
			char ssid[33] = "";
			char password[64] = "";
			strncpy(ssid, body, sizeof(ssid) - 1);
			strncpy(password, nl ? nl + 1 : "", sizeof(password) - 1);
			ssid[sizeof(ssid) - 1] = '\0';          // ⚠ strncpy 不保证 null 终止（超长 body 越界读）
			password[sizeof(password) - 1] = '\0';
			delete[] body;
			wifiConnect(ssid, password);
			sendOk(socketStream);
		}
		else
		{
			sendBadRequest(socketStream);
		}
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/wifiStop", 13))
	{
		sendOk(socketStream);
		socketStream.sendNow();
		if (wifiStationIsStarted()) wifiStationStop();
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/apStart", 12))
	{
		if (!wifiApIsStarted()) wifiApStart();

		if (request.bodyLenght != 0)
		{
			// body 格式: ssid\npassword\n（循环读完，防残留）
			char* body = new char[request.bodyLenght + 1];
			if (!readBodyFully(socketStream, request.bodyLenght, body))
			{
				delete[] body;
				sendBadRequest(socketStream);
				return;
			}
			char* nl = strchr(body, '\n');
			if (nl) *nl = '\0';
			char ssid[33] = "";
			char password[64] = "";
			strncpy(ssid, body, sizeof(ssid) - 1);
			strncpy(password, nl ? nl + 1 : "", sizeof(password) - 1);
			ssid[sizeof(ssid) - 1] = '\0';          // ⚠ strncpy 不保证 null 终止（超长 body 越界读）
			password[sizeof(password) - 1] = '\0';
			delete[] body;
			wifiApSet(ssid, password);
			sendOk(socketStream);
		}
		else
		{
			sendBadRequest(socketStream);
		}
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/apStop", 11))
	{
		socketStream.sendNow();
		sendOk(socketStream);
		if (wifiApIsStarted()) wifiApStop();
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/serverOff", 14))
	{
		sendOk(socketStream);
		socketStream.sendNow();
		vTaskDelay(100 / portTICK_PERIOD_MS);
		socketStream.close();

		serverStop();
		vTaskDelay(1000 / portTICK_PERIOD_MS);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/formatFlash", 16))
	{
		HttpRespond respond;
		{
			respond.cookies.clear();

			respond.heads.clear();
			respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		}

		// 堆分配（原来 VLA char body[n] 在栈上，恶意大 body 会栈溢出）
		char* body = new char[request.bodyLenght + 1];
		if (!readBodyFully(socketStream, request.bodyLenght, body))
		{
			delete[] body;
			sendBadRequest(socketStream);
			return;
		}
		size_t bodyLength = request.bodyLenght;
		ESP_LOGI(TAG, "formating password input: %s", body);
		if (stringCompare((char*)body, bodyLength, FormatingPassword, sizeof(FormatingPassword) - 1))
		{
			ESP_LOGW(TAG, "formating flash");
			formatFlash();
			respond.setBody((void*)"format successfully");
			respond.setBodyLenght(19);
		}
		else
		{
			ESP_LOGW(TAG, "formating password wrong");
			respond.setBody((void*)"wrong password");
			respond.setBodyLenght(14);
		}
		delete[] body;

		respond.send(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/systemRestart", 18))
	{
		sendOk(socketStream);
		socketStream.sendNow();
		ESP_LOGW(TAG, "system restart at 1s");
		vTaskDelay(1000);
		esp_restart();
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/aiConfig", 13))
	{
		// 读取 POST body（JSON，循环读完防残留）
		size_t bodyLen = request.getBodyLenght();
		char* body = new char[bodyLen + 1];
		if (!readBodyFully(socketStream, bodyLen, body))
		{
			delete[] body;
			sendBadRequest(socketStream);
			return;
		}

		cJSON* root = cJSON_Parse(body);
		delete[] body;

		if (!root) {
			sendBadRequest(socketStream);
			return;
		}

		// 保存 ai_config.json（保留未在请求中提供的字段，如 timeout_ms）
		AiConfig curCfg = aiConfigLoad();
		{
			auto readStr = [&](const char* key) -> const char* {
				cJSON* v = cJSON_GetObjectItem(root, key);
				return (v && cJSON_IsString(v) && v->valuestring) ? v->valuestring : "";
			};
			auto readInt = [&](const char* key, int def) -> int {
				cJSON* v = cJSON_GetObjectItem(root, key);
				return (v && cJSON_IsNumber(v)) ? v->valueint : def;
			};

			const char* apiUrl = readStr("api_url");
			const char* model  = readStr("model");
			const char* apiKey = readStr("api_key");
			int timeoutMs = readInt("timeout_ms", curCfg.timeoutMs);
			if (timeoutMs < 5000) timeoutMs = curCfg.timeoutMs;   // 防非法值
			int maxTokens = readInt("max_tokens", curCfg.maxTokens);
			if (maxTokens < 256 || maxTokens > 131072) maxTokens = curCfg.maxTokens;   // 防非法值

			// 写 AI config 到 FAT
			char jsonBuf[512];
			int n = snprintf(jsonBuf, sizeof(jsonBuf),
				"{\n"
				"  \"api_url\": \"%s\",\n"
				"  \"model\": \"%s\",\n"
				"  \"temperature\": 0.7,\n"
				"  \"timeout_ms\": %d,\n"
				"  \"max_tokens\": %d\n"
				"}\n",
				apiUrl[0] ? apiUrl : "https://api.openai.com",
				model[0]  ? model  : "gpt-4o-mini",
				timeoutMs, maxTokens);
			// ⚠ snprintf 返回"本应写入"的长度，api_url/model 超长时可能 > 缓冲 →
			// 写入长度必须截断到缓冲内，否则 file.write 越界读
			if (n >= (int)sizeof(jsonBuf)) n = (int)sizeof(jsonBuf) - 1;

			OFile file;
			if (file.open("/root/system/ai_config.json")) {
				file.write(jsonBuf, n);
				file.close();
			}

			// 写 API Key 到 NVS
			if (apiKey[0]) {
				aiConfigSaveApiKey(apiKey);
			}

			sendOk(socketStream);
		}

		cJSON_Delete(root);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/mdnsConfig", 15))
	{
		// 读取 POST body（JSON，循环读完防残留）
		size_t bodyLen = request.getBodyLenght();
		char* body = new char[bodyLen + 1];
		if (!readBodyFully(socketStream, bodyLen, body))
		{
			delete[] body;
			sendBadRequest(socketStream);
			return;
		}

		cJSON* root = cJSON_Parse(body);
		delete[] body;

		if (!root) {
			sendBadRequest(socketStream);
			return;
		}

		cJSON* hostnameJson = cJSON_GetObjectItem(root, "hostname");
		if (hostnameJson && cJSON_IsString(hostnameJson) && hostnameJson->valuestring)
		{
			if (mdnsSetHostname(hostnameJson->valuestring))
			{
				sendOk(socketStream);
			}
			else
			{
				HttpRespond respond;
				respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
				respond.setStatus(HttpStatus::BadRequest);
				respond.setReason(HttpReason::BadRequest);
				const char* errMsg = "主机名无效（仅允许字母、数字、连字符）";
				respond.setBody((void*)errMsg);
				respond.setBodyLenght(strlen(errMsg));
				respond.send(socketStream);
			}
		}
		else
		{
			sendBadRequest(socketStream);
		}

		cJSON_Delete(root);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/resetInfo/clear", 20))
	{
		// 读掉 body（若带 body 不消费会残留，被下一请求误当请求行）
		if (request.getBodyLenght() > 0)
		{
			// ⚠ readBodyFully 末尾写 buf[len]='\0'，必须分配 len+1（此前 1 字节堆越界写）
			char* tmp = new char[request.getBodyLenght() + 1];
			if (!readBodyFully(socketStream, request.getBodyLenght(), tmp))
			{
				delete[] tmp;
				sendBadRequest(socketStream);
				return;
			}
			delete[] tmp;
		}
		LogService::instance().clearCrashRecords();
		sendOk(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/logLevel", 13))
	{
		// body（0-5：NONE..VERBOSE）→ NVS 持久化
		//   全局/分级：{"level":4,"file_level":1,"ws_level":3}（缺省不改动）
		//   按 tag 源头过滤：{"tag":"BleGamepad","level":1}（level 缺省=移除该 tag 设置）
		//   流程：esp_log 源头筛选（vprintf 前）→ LogService 入队 → fat/ws 分级（file/ws_level）
		char* body = new char[request.bodyLenght + 1];
		if (!readBodyFully(socketStream, request.bodyLenght, body))
		{
			delete[] body;
			sendBadRequest(socketStream);
			return;
		}
		cJSON* root = cJSON_Parse(body);
		delete[] body;
		if (!root)
		{
			sendBadRequest(socketStream);
			return;
		}

		auto clampLevel = [](int v) -> int {
			if (v < 0) return 0;
			if (v > 5) return 5;
			return v;
		};

		const cJSON* tag = cJSON_GetObjectItem(root, "tag");
		if (cJSON_IsString(tag) && tag->valuestring && tag->valuestring[0])
		{
			/* 按 tag 源头过滤：esp_log_level_set(tag, level) 立即生效 + NVS 持久化 */
			const cJSON* level = cJSON_GetObjectItem(root, "level");
			if (cJSON_IsNumber(level))
				LogService::instance().setTagLevel(tag->valuestring, clampLevel(level->valueint));
			else
				LogService::instance().clearTagLevel(tag->valuestring);
		}
		else
		{
			const cJSON* level = cJSON_GetObjectItem(root, "level");
			if (cJSON_IsNumber(level))
				LogService::instance().setGlobalLevel(clampLevel(level->valueint));
			const cJSON* fileLevel = cJSON_GetObjectItem(root, "file_level");
			if (cJSON_IsNumber(fileLevel))
				LogService::instance().setFileLevel(clampLevel(fileLevel->valueint));
			const cJSON* wsLevel = cJSON_GetObjectItem(root, "ws_level");
			if (cJSON_IsNumber(wsLevel))
				LogService::instance().setWsLevel(clampLevel(wsLevel->valueint));
		}

		cJSON_Delete(root);
		sendOk(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/coredump/clear", 19))
	{
		// 读掉 body（若带 body 不消费会残留，被下一请求误当请求行）
		if (request.getBodyLenght() > 0)
		{
			// ⚠ readBodyFully 末尾写 buf[len]='\0'，必须分配 len+1（此前 1 字节堆越界写）
			char* tmp = new char[request.getBodyLenght() + 1];
			if (!readBodyFully(socketStream, request.getBodyLenght(), tmp))
			{
				delete[] tmp;
				sendBadRequest(socketStream);
				return;
			}
			delete[] tmp;
		}

		struct Ctx { esp_err_t err; };
		Ctx ctx{ ESP_FAIL };
		OtaManager::instance().runInInternalStack([](void* p) {
			((Ctx*)p)->err = esp_core_dump_image_erase();
		}, &ctx);

		if (ctx.err == ESP_OK) sendOk(socketStream);
		else sendBadRequest(socketStream);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/ota/setBoot", 16))
	{
		httpOtaSetBoot(socketStream, request);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/ota/confirm", 16))
	{
		httpOtaConfirm(socketStream, request);
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/ota", 8))
	{
		httpOta(socketStream, request);
	}
	else
	{
		sendBadRequest(socketStream);
		return;
	}
}
void httpOta(IOSocketStream& socketStream, HttpRequest& request)
{
	// httpOta 内部大量调用 OTA API（esp_ota_begin/end、get_last_invalid 等）会 mmap flash
	// （禁用 cache 要求栈在内部 RAM），coWorker 栈在 PSRAM → 经 runInInternalStack 执行（阻塞）。
	// ⚠ 写缓冲（4KB）+ 镜像头（512B）用 getWriteBuffer/getHeaderBuffer 提供的**内部 RAM
	//   静态缓冲**（.bss）——写 flash 禁 cache 窗口内 PSRAM 不可访问；静态缓冲避免堆损坏与栈溢出。
	//   （曾踩坑：栈上大数组→栈溢出；heap_caps_malloc 内部堆→堆相邻块被踩 Bad tail。）
	struct OtaCtx { IOSocketStream* stream; HttpRequest* request; };
	OtaCtx ctx{ &socketStream, &request };
	OtaManager::instance().runInInternalStack([](void* p) { httpOtaInternal((OtaCtx*)p); }, &ctx);
}

static void httpOtaInternal(void* p)
{
	struct OtaCtx { IOSocketStream* stream; HttpRequest* request; };
	auto* c = (OtaCtx*)p;
	IOSocketStream& socketStream = *c->stream;
	HttpRequest& request = *c->request;

	OtaManager& ota = OtaManager::instance();

	auto sendError = [&](HttpStatus status, const char* msg) {
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		respond.setStatus(status);
		respond.setReason(status == HttpStatus::BadRequest ? HttpReason::BadRequest : HttpReason::InternalServerError);
		respond.setBody((void*)msg);
		respond.setBodyLenght(strlen(msg));
		respond.send(socketStream);
	};

	// 内部 RAM 静态缓冲
	uint8_t* header = ota.getHeaderBuffer();
	uint8_t* buffer = ota.getWriteBuffer();

	// 并发保护：同一时间只允许一个 OTA（6 个 worker 线程互斥）
	if (ota.isUpdating())
	{
		sendError(HttpStatus::BadRequest, "OTA 已在升级中，请稍候重试");
		return;
	}
	ota.setUpdating(true);
	ESP_LOGI(TAG, "otaInt: 并发标志已置位");

	// 目标分区检查
	if (ota.getNextUpdatePartition() == nullptr)
	{
		ota.setUpdating(false);
		sendError(HttpStatus::BadRequest, "无可用 OTA 分区");
		return;
	}
	ESP_LOGI(TAG, "otaInt: 目标分区 OK");

	// 大小预检
	const size_t total = request.getBodyLenght();
	if (total > ota.getUpdatePartitionSize())
	{
		ota.setUpdating(false);
		sendError(HttpStatus::BadRequest, "固件过大，超过目标分区容量");
		return;
	}
	ESP_LOGI(TAG, "otaInt: 大小预检 OK (%u 字节)", (unsigned)total);

	// 读镜像头（覆盖 app_desc）→ 预检（magic + 版本防重烧）
	size_t headerGot = 0;
	size_t left = total;
	while (headerGot < OtaManager::HeaderSize && left > 0)
	{
		size_t got = socketStream.readByte((char*)header + headerGot, OtaManager::HeaderSize - headerGot);
		if (got == 0)
		{
			if (!socketStream.isGood())
			{
				ota.setUpdating(false);
				sendError(HttpStatus::InternalServerError, "连接中断");
				return;
			}
			vTaskDelay(1);
			continue;
		}
		headerGot += got;
		left -= got;
	}
	ESP_LOGI(TAG, "otaInt: 镜像头读取 OK (%u)", (unsigned)headerGot);

	if (ota.precheckImage(header, headerGot) != ESP_OK)
	{
		ota.setUpdating(false);
		sendError(HttpStatus::BadRequest, "镜像无效，或版本与上次升级失败版本相同");
		return;
	}
	ESP_LOGI(TAG, "otaInt: 镜像预检 OK");

	// 开始 OTA（记录 pending 目标槽，供重启后判定回滚）
	void* otaHandle = nullptr;
	if (ota.begin(&otaHandle) != ESP_OK)
	{
		ota.setUpdating(false);
		sendError(HttpStatus::InternalServerError, "OTA 初始化失败");
		return;
	}
	ESP_LOGI(TAG, "otaInt: otaBegin OK");

	// 写入已读的头部
	if (headerGot > 0 && ota.write(otaHandle, header, headerGot) != ESP_OK)
	{
		ota.abort(otaHandle);
		ota.setUpdating(false);
		sendError(HttpStatus::InternalServerError, "OTA 写入失败");
		return;
	}

	// 流式写入剩余数据（socket 非阻塞，readByte 返回 0 时让出 CPU）
	size_t chunk = 0;
	while (left > 0)
	{
		size_t got = socketStream.readByte((char*)buffer, 4096);
		if (got == 0)
		{
			if (!socketStream.isGood())
			{
				ota.abort(otaHandle);
				ota.setUpdating(false);
				sendError(HttpStatus::InternalServerError, "连接中断，OTA 中止");
				return;
			}
			vTaskDelay(1);
			continue;
		}
		if (ota.write(otaHandle, buffer, got) != ESP_OK)
		{
			ota.abort(otaHandle);
			ota.setUpdating(false);
			sendError(HttpStatus::InternalServerError, "OTA 写入失败");
			return;
		}
		left -= got;
		if ((chunk++ & 0x1F) == 0)
			ESP_LOGI(TAG, "otaInt: 已写入 %u / %u", (unsigned)(total - left), (unsigned)total);
		vTaskDelay(1);
	}
	ESP_LOGI(TAG, "otaInt: 流式写入完成");

	// 校验镜像 + 设置启动分区
	if (ota.finish(otaHandle) != ESP_OK)
	{
		ota.setUpdating(false);
		sendError(HttpStatus::BadRequest, "固件镜像校验失败，已保留当前版本");
		return;
	}

	ota.setUpdating(false);

	// 成功：应答后延迟重启
	{
		HttpRespond respond;
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		const char* okMsg = "OTA 升级成功，设备即将重启";
		respond.setBody((void*)okMsg);
		respond.setBodyLenght(strlen(okMsg));
		respond.send(socketStream);
	}
	socketStream.sendNow();
	ESP_LOGI(TAG, "OTA 成功，1s 后重启");
	vTaskDelay(1000);
	esp_restart();
}

// ── POST /api/ota/setBoot — 手动切换下次启动分区（web「分区管理」）──
// body: {"slot":"ota_1","invalidate":false}，成功默认 1s 后重启
void httpOtaSetBoot(IOSocketStream& socketStream, HttpRequest& request)
{
	// 读 body（JSON）——循环读到完，防残留
	size_t bodyLen = request.getBodyLenght();
	char* body = new char[bodyLen + 1];
	if (!readBodyFully(socketStream, bodyLen, body))
	{
		delete[] body;
		sendBadRequest(socketStream);
		return;
	}

	cJSON* root = cJSON_Parse(body);
	delete[] body;

	if (!root)
	{
		sendBadRequest(socketStream);
		return;
	}

	char slot[16] = "";
	bool invalidate = false;
	if (cJSON* v = cJSON_GetObjectItem(root, "slot"))
		if (cJSON_IsString(v) && v->valuestring)
			strncpy(slot, v->valuestring, sizeof(slot) - 1);
	if (cJSON* v = cJSON_GetObjectItem(root, "invalidate"))
		invalidate = cJSON_IsTrue(v);
	cJSON_Delete(root);

	if (slot[0] == '\0')
	{
		sendBadRequest(socketStream);
		return;
	}

	// 切换会 mmap flash（禁用 cache 要求内部 RAM 栈）→ 经 runInInternalStack 执行。
	// slot 在调用者栈（PSRAM）：worker 仅在 mmap 窗口外读取（strcmp 发生在库调用之前），安全。
	struct Ctx { char slot[16]; bool invalidate; esp_err_t err; };
	Ctx ctx{};
	strncpy(ctx.slot, slot, sizeof(ctx.slot) - 1);
	ctx.invalidate = invalidate;
	ctx.err = ESP_FAIL;

	OtaManager::instance().runInInternalStack([](void* p) {
		auto* c = (Ctx*)p;
		c->err = OtaManager::instance().switchBootPartition(c->slot, c->invalidate);
	}, &ctx);

	if (ctx.err != ESP_OK)
	{
		const char* msg = "切换失败：分区不存在、无有效镜像或已是当前分区";
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		respond.setStatus(HttpStatus::BadRequest);
		respond.setReason(HttpReason::BadRequest);
		respond.setBody((void*)msg);
		respond.setBodyLenght(strlen(msg));
		respond.send(socketStream);
		return;
	}

	// 成功：应答后延迟重启
	{
		HttpRespond respond;
		respond.setStatus(HttpStatus::Ok);
		respond.setReason(HttpReason::Ok);
		const char* okMsg = "切换成功，设备即将重启到目标分区";
		respond.setBody((void*)okMsg);
		respond.setBodyLenght(strlen(okMsg));
		respond.send(socketStream);
	}
	socketStream.sendNow();
	ESP_LOGI(TAG, "切换启动分区成功，1s 后重启");
	vTaskDelay(1000);
	esp_restart();
}

// ── POST /api/ota/confirm — 手动确认当前固件（关闭回滚窗口，无需重启）──
void httpOtaConfirm(IOSocketStream& socketStream, HttpRequest& request)
{
	// 读掉 body（若带 body 不消费会残留，被下一请求误当请求行）
	if (request.getBodyLenght() > 0)
	{
		// ⚠ readBodyFully 末尾写 buf[len]='\0'，必须分配 len+1（此前 1 字节堆越界写）
		char* tmp = new char[request.getBodyLenght() + 1];
		if (!readBodyFully(socketStream, request.getBodyLenght(), tmp))
		{
			delete[] tmp;
			sendBadRequest(socketStream);
			return;
		}
		delete[] tmp;
	}

	struct Ctx { esp_err_t err; };
	Ctx ctx{ ESP_FAIL };
	OtaManager::instance().runInInternalStack([](void* p) {
		((Ctx*)p)->err = OtaManager::instance().confirmRunning();
	}, &ctx);

	if (ctx.err != ESP_OK)
	{
		const char* msg = "确认失败：当前固件状态异常";
		HttpRespond respond;
		respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });
		respond.setStatus(HttpStatus::BadRequest);
		respond.setReason(HttpReason::BadRequest);
		respond.setBody((void*)msg);
		respond.setBodyLenght(strlen(msg));
		respond.send(socketStream);
		return;
	}

	const char* okMsg = "已确认当前固件，回滚窗口关闭";
	HttpRespond respond;
	respond.setStatus(HttpStatus::Ok);
	respond.setReason(HttpReason::Ok);
	respond.setBody((void*)okMsg);
	respond.setBodyLenght(strlen(okMsg));
	respond.send(socketStream);
}

void httpPut(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = request.getPath();
	char* path = new char[sizeof(PerfixRoot) + strlen(uri)];
	size_t pathLength = sprintf(path, "%s%s", PerfixRoot, uri);

	if (path[pathLength - 1] == '/')
	{
		//floor
		newFloor(path);
		// tree(PerfixRoot); //[debug]
		delete[] path;

		// ⚠ 建目录无需 body，但必须消费完：readByte 非阻塞单次读不满，残留 body 会被
		//   同一连接的下一个请求误当请求行（曾出现 POST "name": 诡异请求——客户端把
		//   manifest 内容 PUT 到目录 URL 时 body 196B 全部残留）。空 body（file 页面
		//   newFloor 的 send("")）不受影响。
		size_t left = request.getBodyLenght();
		char discard[PutBufferSize];
		bool drained = true;
		while (left > 0)
		{
			size_t got = socketStream.readByte(discard, left < PutBufferSize ? left : PutBufferSize);
			if (got == 0)
			{
				if (!socketStream.isGood()) { drained = false; break; }
				vTaskDelay(1);
				continue;
			}
			left -= got;
		}
		if (drained) sendOk(socketStream);
	}
	else
	{
		//file
		bool tooLarge = true;
		if (prefixCompare(path, pathLength, PrefixSd, strlen(PrefixSd)))
		{
			tooLarge = request.getBodyLenght() > SdPutMaxSize;
		}
		else if (prefixCompare(path, pathLength, PerfixMem, strlen(PerfixMem)))
		{
			tooLarge = request.getBodyLenght() > MemPutMaxSize;
		}
		else // flash
		{
			tooLarge = request.getBodyLenght() > FlashPutMaxSize ||
				request.getBodyLenght() > getFreeSpace();
		}

		if (tooLarge)
		{
			ESP_LOGW(TAG, "put file is too large");

			HttpRespond respond;
			respond.setBody((void*)"file too large");
			respond.setBodyLenght(14);
			respond.setStatus(HttpStatus::BadRequest);
			respond.setReason(HttpReason::BadRequest);
			respond.send(socketStream);

			vTaskDelay(100 / portTICK_PERIOD_MS);
			socketStream.close();
			delete[] path;   // ⚠ 此前泄漏：超大文件 PUT 每次累积 path（其他分支均有释放）
			return;
		}

		newFile(path);
		OFile file;
		file.open(path);
		if (!file.isOpen())
		{
			ESP_LOGE(TAG, "error in put file %s", path);

			HttpRespond respond;
			respond.setBody((void*)HttpReason::InternalServerError);
			respond.setBodyLenght(sizeof(HttpReason::InternalServerError) - 1);
			respond.setStatus(HttpStatus::InternalServerError);
			respond.setReason(HttpReason::InternalServerError);
			respond.send(socketStream);

			vTaskDelay(100 / portTICK_PERIOD_MS);
			socketStream.close();
			return;
		}

		size_t left = request.getBodyLenght();
		size_t readCount = 0;
		char buffer[PutBufferSize] = "";

		while (left > 0)
		{
			readCount = socketStream.readByte(buffer, PutBufferSize);
			if (readCount == 0)
			{
				// readByte 非阻塞：无数据或连接断开均返回 0。
				// 断连时若不退出，worker 会被永久占住（6 个 worker 占满即服务器瘫痪）。
				if (!socketStream.isGood())
				{
					ESP_LOGW(TAG, "put connection lost, abort");
					file.close();
					removeFile(path);
					sendBadRequest(socketStream);
					delete[] path;
					return;
				}
				vTaskDelay(1);
				continue;
			}
			file.write(buffer, readCount);
			left -= readCount;
			vTaskDelay(1);
		}

		// tree(PerfixRoot); //[debug]

#if false
		printf("coworker:httpPut(): stack high water = %u\n", uxTaskGetStackHighWaterMark(nullptr));
#endif

		delete[] path;   // ⚠ 成功路径释放（此前泄漏，反复上传累积）
		sendOk(socketStream);
	}
}

void httpDelete(IOSocketStream& socketStream, HttpRequest& request)
{
	// ⚠ 伪造超大 Content-Length → new 失败崩溃。DELETE body 是路径，上限 512。
	if (request.getBodyLenght() > kMaxDeletePath)
	{
		sendBadRequest(socketStream);
		return;
	}
	char* path = new char[sizeof(PerfixRoot) + request.getBodyLenght()];
	strcpy(path, PerfixRoot);
	size_t pathLength = sizeof(PerfixRoot) - 1;
	// 循环读到完（readByte 单次可能读不满，残留会被下个请求误当请求行）
	if (!readBodyFully(socketStream, request.getBodyLenght(), path + pathLength))
	{
		delete[] path;
		sendBadRequest(socketStream);
		return;
	}
	pathLength += request.getBodyLenght();

	if (path[pathLength - 1] == '/')
	{
		//floor
		removeFloor(path);
	}
	else
	{
		//file
		removeFile(path);
	}
	// tree(PerfixRoot); //[debug]
	delete[] path;   // ⚠ 成功路径释放（此前泄漏）
	sendOk(socketStream);
}

void restart()
{
	if (!serverRunning) return;
	autoRestartTimes++;
	if (autoRestartTimes < maxRestartTimes)
	{
		Task::addTask([](void*) -> TickType_t {
			ESP_LOGW(TAG, "restart at %d times", autoRestartTimes);
			serverHandle = xTaskCreateStatic(server, "server", 4096, nullptr, Task::Priority::Deamon, serverStack, &serverTask);
			return Task::infinityTime;
			}, "server restart", nullptr, 1000);
	}
	else
	{
		ESP_LOGE(TAG, "restart %u times, which is the max times", autoRestartTimes);
		serverRunning = false;
	}
}

void apiFloor(OSocketStream& socketStream, const char* path)
{
	HttpRespond respond;
	respond.cookies.clear();
	respond.heads.clear();
	respond.heads.add({ "Content-Type", " text/plain; charset=utf-8" });

	Floor floor;
	floor.open(path);
	floor.reCount();

	size_t contentCount[2] = { floor.getCount(Floor::Type::Floor), floor.getCount(Floor::Type::File) };
	size_t totolSize = 0;
	const char* buffer = nullptr; //no need to free
	char* body = nullptr; //new later
	size_t bodyOffset = 0;

	//统计总字符数（⚠ 计数阶段与写入阶段同样判空——6 worker 并发 PUT/DELETE
	// 改目录内容时 readdir 耗尽返回 nullptr，strlen(nullptr) 直接崩溃）
	for (size_t i = 0; i < contentCount[0]; i++)
	{
		buffer = floor.read(Floor::Type::Floor);
		if (buffer == nullptr)
		{
			ESP_LOGW(TAG, "apiFloor: read null in floor %s", path);
			contentCount[0] = 0;
			contentCount[1] = 0;
			totolSize = 0;
			break;
		}
		totolSize += strlen(buffer) + 2; // one for '/' and another for '\n'
	}

	floor.backToBegin();
	for (size_t i = 0; i < contentCount[1]; i++)
	{
		buffer = floor.read(Floor::Type::File);
		if (buffer == nullptr)
		{
			ESP_LOGW(TAG, "apiFloor: read null in floor %s", path);
			contentCount[0] = 0;
			contentCount[1] = 0;
			totolSize = 0;
			break;
		}
		totolSize += strlen(buffer) + 1; // just one for '\n'
	}

	//写入内存
	floor.backToBegin();
	body = new char[totolSize]; //body is new here
	for (size_t i = 0;i < contentCount[0];i++)
	{
		buffer = floor.read(Floor::Type::Floor);
		if (buffer == nullptr)
		{
			ESP_LOGW(TAG, "apiFloor: read null in floor %s", path);
			totolSize = 0;
			contentCount[0] = 0;
			contentCount[1] = 0;
			continue;
		}
		// ⚠ 并发 PUT/DELETE 使目录条目变多/名字变长时，预分配的 totolSize 可能不够 →
		//   堆越界写。写入前检查剩余容量，超限即截断（宁缺勿滥，防崩溃）。
		size_t need = strlen(buffer) + 2;   // '/' + '\n'
		if (bodyOffset + need > totolSize) break;
		for (size_t j = 0; buffer[j] != '\0'; j++)
		{
			body[bodyOffset] = buffer[j];
			bodyOffset++;
		}
		body[bodyOffset] = '/';
		bodyOffset++;
		body[bodyOffset] = '\n';
		bodyOffset++;
	}
	floor.backToBegin();
	for (size_t i = 0;i < contentCount[1];i++)
	{
		buffer = floor.read(Floor::Type::File);
		if (buffer == nullptr)
		{
			ESP_LOGW(TAG, "apiFloor: read null in floor %s", path);
			totolSize = 0;
			contentCount[0] = 0;
			contentCount[1] = 0;
			continue;
		}
		size_t need = strlen(buffer) + 1;   // '\n'
		if (bodyOffset + need > totolSize) break;
		for (size_t j = 0; buffer[j] != '\0'; j++)
		{
			body[bodyOffset] = buffer[j];
			bodyOffset++;
		}
		body[bodyOffset] = '\n';
		bodyOffset++;
	}

	respond.setBody((void*)body);
	if (totolSize > 1) respond.setBodyLenght(totolSize - 1); //去除最后一个'\n'
	else respond.setBodyLenght(0);
	respond.send(socketStream);

	delete[] body; //body is deleted here
}
