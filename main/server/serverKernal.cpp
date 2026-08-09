#include <time.h>

#include <esp_task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <lwip/sockets.h>

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

constexpr size_t SocketStreamWindowNumber = 16;
constexpr size_t CoworkerNumber = 6;

constexpr size_t WifiApRecordSize = 50;

EXT_RAM_BSS_ATTR Socket serverListenSocket{};
EXT_RAM_BSS_ATTR bool serverRunning = false;
EXT_RAM_BSS_ATTR SocketStreamWindow socketStreamWindows[SocketStreamWindowNumber]{};
EXT_RAM_BSS_ATTR static Mutex scanMutex{};
EXT_RAM_BSS_ATTR static size_t scanPosition = 0;

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
		printf("server:socket creat failed: error %d\n", serverListenSocket);
		serverRunning = false;
		vTaskDelete(nullptr);
	}

	int opt = 1;
	setsockopt(serverListenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	printf("socket created\n");

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
		printf("server: socket bind failed: errno %d\n", errno);
		serverRunning = false;
		vTaskDelete(nullptr);
	}
	printf("server:socket bound, port 80\n");

	err = listen(serverListenSocket, 1);
	if (err != 0)
	{
		printf("server: socket listen failed: errno %d\n", errno);
		serverRunning = false;
		vTaskDelete(nullptr);
	}

	while (serverRunning)
	{
		printf("server: socket listening\n");

		struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
		socklen_t addr_len = sizeof(source_addr);
		int sock = accept(serverListenSocket, (struct sockaddr*)&source_addr, &addr_len);
		if (sock < 0)
		{
			printf("server: Unable to accept connection: errno %d\n", errno);
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
		char addr_str[128];
		if (source_addr.ss_family == PF_INET)
		{
			inet_ntoa_r(((sockaddr_in*)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
		}

		printf("server: socket accepted ip address: %s\n", addr_str);

		size_t index = 0;
		while (!socketStreamWindows[index].setSocket(sock))
		{
			index++;
			if (index >= SocketStreamWindowNumber)
			{
				index = 0;
				printf("server: all window was occupyed\n");
				vTaskDelay(100 / portTICK_PERIOD_MS);
			}
		}
		printf("SocketStreamWindow state: %d / %d\n", SocketStreamWindow::getEnabledCount(), SocketStreamWindowNumber);
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

	printf("Coworker %d started\n", (int)coworkerIndex);
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
					scanPosition = index + 1 % SocketStreamWindowNumber;
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
	printf("Coworker %d ended\n", (int)coworkerIndex);
	coWorkerHandle[(int)coworkerIndex] = nullptr;
	vTaskDelete(NULL);
}

void recieve(IOSocketStream& socketStream)
{
	HttpRequest request;
	request.receive(socketStream);

	// print request infomation
	{
		printf("server::recieve: method = %s\n", getMethodNameFromMethod(request.getMethod()));
		printf("server::recieve: path = %s\n", request.getPath());

		HttpStringPairSize_t headNumber = request.heads.getNumber();
		for (HttpStringPairSize_t i = 0; i < headNumber; i++)
		{
			const char* name = request.heads[i].getName();
			const char* value = request.heads[i].getValue();
			printf("server::recieve: head[%d] is %s : %s\n", i, name, value);
		}

		HttpStringPairSize_t cookieNumber = request.cookies.getNumber();
		for (HttpStringPairSize_t i = 0; i < cookieNumber; i++)
		{
			printf("server::recieve: cookie[%d] is %s = %s\n", i, request.cookies[i].getName(), request.cookies[i].getValue());
		}

		printf("server::recieve: body lenght = %u\n", request.getBodyLenght());
		printf("\n");
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
		printf("server::recieve: socketStream error\n");
}

void sendOk(OSocketStream& socketStream)
{
	HttpRespond respond;
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
		// otaGetInfoJson 内部 mmap flash（禁用 cache 要求栈在内部 RAM），coWorker 栈在 PSRAM →
		// 经 otaRunInInternalStack 在内部 RAM 栈任务中执行（阻塞）。
		char body[1024];
		struct Ctx { char* buf; size_t size; };
		Ctx ctx{ body, sizeof(body) };
		otaRunInInternalStack([](void* p) {
			auto* c = (Ctx*)p;
			otaGetInfoJson(c->buf, c->size);
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

		// 统计长度
		// unsigned char wifiRssiLenght[wifiApRecordSize] = {0};
		// rssi假定为3位，“-85”
		size_t bodyLenght = 0;
		for (size_t i = 0;i < count;i++)
		{
			bodyLenght += strlen((char*)wifi[i].ssid) + 5;//rssi的3位，':'的1位，'\n'的1位
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
		printf("formating password input: %s\n", body);
		if (stringCompare((char*)body, bodyLength, FormatingPassword, sizeof(FormatingPassword) - 1))
		{
			printf("formating flash\n");
			formatFlash();
			respond.setBody((void*)"format successfully");
			respond.setBodyLenght(19);
		}
		else
		{
			printf("formating password wrong\n");
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
		printf("system restart at 1s\n");
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
	// （禁用 cache 要求栈在内部 RAM），coWorker 栈在 PSRAM → 经 otaRunInInternalStack 执行（阻塞）。
	// ⚠ 写缓冲（4KB）+ 镜像头（512B）用 otaGetWriteBuffer/otaGetHeaderBuffer 提供的**内部 RAM
	//   静态缓冲**（.bss）——写 flash 禁 cache 窗口内 PSRAM 不可访问；静态缓冲避免堆损坏与栈溢出。
	//   （曾踩坑：栈上大数组→栈溢出；heap_caps_malloc 内部堆→堆相邻块被踩 Bad tail。）
	struct OtaCtx { IOSocketStream* stream; HttpRequest* request; };
	OtaCtx ctx{ &socketStream, &request };
	otaRunInInternalStack([](void* p) { httpOtaInternal((OtaCtx*)p); }, &ctx);
}

static void httpOtaInternal(void* p)
{
	struct OtaCtx { IOSocketStream* stream; HttpRequest* request; };
	auto* c = (OtaCtx*)p;
	IOSocketStream& socketStream = *c->stream;
	HttpRequest& request = *c->request;

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
	uint8_t* header = otaGetHeaderBuffer();
	uint8_t* buffer = otaGetWriteBuffer();

	// 并发保护：同一时间只允许一个 OTA（6 个 worker 线程互斥）
	if (otaIsUpdating())
	{
		sendError(HttpStatus::BadRequest, "OTA 已在升级中，请稍候重试");
		return;
	}
	otaSetUpdating(true);
	printf("otaInt: 并发标志已置位\n");

	// 目标分区检查
	if (otaGetNextUpdatePartition() == nullptr)
	{
		otaSetUpdating(false);
		sendError(HttpStatus::BadRequest, "无可用 OTA 分区");
		return;
	}
	printf("otaInt: 目标分区 OK\n");

	// 大小预检
	const size_t total = request.getBodyLenght();
	if (total > otaGetUpdatePartitionSize())
	{
		otaSetUpdating(false);
		sendError(HttpStatus::BadRequest, "固件过大，超过目标分区容量");
		return;
	}
	printf("otaInt: 大小预检 OK (%u 字节)\n", (unsigned)total);

	// 读镜像头（覆盖 app_desc）→ 预检（magic + 版本防重烧）
	size_t headerGot = 0;
	size_t left = total;
	while (headerGot < OtaHeaderSize && left > 0)
	{
		size_t got = socketStream.readByte((char*)header + headerGot, OtaHeaderSize - headerGot);
		if (got == 0)
		{
			if (!socketStream.isGood())
			{
				otaSetUpdating(false);
				sendError(HttpStatus::InternalServerError, "连接中断");
				return;
			}
			vTaskDelay(1);
			continue;
		}
		headerGot += got;
		left -= got;
	}
	printf("otaInt: 镜像头读取 OK (%u)\n", (unsigned)headerGot);

	if (otaPrecheckImage(header, headerGot) != ESP_OK)
	{
		otaSetUpdating(false);
		sendError(HttpStatus::BadRequest, "镜像无效，或版本与上次升级失败版本相同");
		return;
	}
	printf("otaInt: 镜像预检 OK\n");

	// 开始 OTA（记录 pending 目标槽，供重启后判定回滚）
	void* otaHandle = nullptr;
	if (otaBegin(&otaHandle) != ESP_OK)
	{
		otaSetUpdating(false);
		sendError(HttpStatus::InternalServerError, "OTA 初始化失败");
		return;
	}
	printf("otaInt: otaBegin OK\n");

	// 写入已读的头部
	if (headerGot > 0 && otaWrite(otaHandle, header, headerGot) != ESP_OK)
	{
		otaAbort(otaHandle);
		otaSetUpdating(false);
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
				otaAbort(otaHandle);
				otaSetUpdating(false);
				sendError(HttpStatus::InternalServerError, "连接中断，OTA 中止");
				return;
			}
			vTaskDelay(1);
			continue;
		}
		if (otaWrite(otaHandle, buffer, got) != ESP_OK)
		{
			otaAbort(otaHandle);
			otaSetUpdating(false);
			sendError(HttpStatus::InternalServerError, "OTA 写入失败");
			return;
		}
		left -= got;
		if ((chunk++ & 0x1F) == 0)
			printf("otaInt: 已写入 %u / %u\n", (unsigned)(total - left), (unsigned)total);
		vTaskDelay(1);
	}
	printf("otaInt: 流式写入完成\n");

	// 校验镜像 + 设置启动分区
	if (otaFinish(otaHandle) != ESP_OK)
	{
		otaSetUpdating(false);
		sendError(HttpStatus::BadRequest, "固件镜像校验失败，已保留当前版本");
		return;
	}

	otaSetUpdating(false);

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
	printf("server: OTA 成功，1s 后重启\n");
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

	// 切换会 mmap flash（禁用 cache 要求内部 RAM 栈）→ 经 otaRunInInternalStack 执行。
	// slot 在调用者栈（PSRAM）：worker 仅在 mmap 窗口外读取（strcmp 发生在库调用之前），安全。
	struct Ctx { char slot[16]; bool invalidate; esp_err_t err; };
	Ctx ctx{};
	strncpy(ctx.slot, slot, sizeof(ctx.slot) - 1);
	ctx.invalidate = invalidate;
	ctx.err = ESP_FAIL;

	otaRunInInternalStack([](void* p) {
		auto* c = (Ctx*)p;
		c->err = otaSwitchBootPartition(c->slot, c->invalidate);
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
	printf("server: 切换启动分区成功，1s 后重启\n");
	vTaskDelay(1000);
	esp_restart();
}

// ── POST /api/ota/confirm — 手动确认当前固件（关闭回滚窗口，无需重启）──
void httpOtaConfirm(IOSocketStream& socketStream, HttpRequest& request)
{
	struct Ctx { esp_err_t err; };
	Ctx ctx{ ESP_FAIL };
	otaRunInInternalStack([](void* p) {
		((Ctx*)p)->err = otaConfirmRunning();
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
		sendOk(socketStream);
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
			printf("server: put file is too large\n");

			HttpRespond respond;
			respond.setBody((void*)"file too large");
			respond.setBodyLenght(14);
			respond.setStatus(HttpStatus::BadRequest);
			respond.setReason(HttpReason::BadRequest);
			respond.send(socketStream);

			vTaskDelay(100 / portTICK_PERIOD_MS);
			socketStream.close();
			return;
		}

		newFile(path);
		OFile file;
		file.open(path);
		if (!file.isOpen())
		{
			printf("server: error in put file %s\n", path);

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
			file.write(buffer, readCount);
			left -= readCount;
			vTaskDelay(1);
		}

		// tree(PerfixRoot); //[debug]

#if false
		printf("coworker:httpPut(): stack high water = %u\n", uxTaskGetStackHighWaterMark(nullptr));
#endif

		sendOk(socketStream);
	}
}

void httpDelete(IOSocketStream& socketStream, HttpRequest& request)
{
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
	sendOk(socketStream);
}

void restart()
{
	if (!serverRunning) return;
	autoRestartTimes++;
	if (autoRestartTimes < maxRestartTimes)
	{
		Task::addTask([](void*) -> TickType_t {
			printf("server: restart at %d times\n", autoRestartTimes);
			serverHandle = xTaskCreateStatic(server, "server", 4096, nullptr, Task::Priority::Deamon, serverStack, &serverTask);
			return Task::infinityTime;
			}, "server restart", nullptr, 1000);
	}
	else
	{
		printf("restart %u times, which is the max times\n", autoRestartTimes);
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

	//统计总字符数
	for (size_t i = 0; i < contentCount[0]; i++)
	{
		buffer = floor.read(Floor::Type::Floor);
		totolSize += strlen(buffer) + 2; // one for '/' and another for '\n'
	}

	floor.backToBegin();
	for (size_t i = 0; i < contentCount[1]; i++)
	{
		buffer = floor.read(Floor::Type::File);
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
			printf("server: apiFloor: read null in floor %s", path);
			totolSize = 0;
			contentCount[0] = 0;
			contentCount[1] = 0;
			continue;
		}
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
			printf("server: apiFloor: read null in floor %s", path);
			totolSize = 0;
			contentCount[0] = 0;
			contentCount[1] = 0;
			continue;
		}
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
