#include <time.h>

#include <esp_task.h>
#include <lwip/sockets.h>

#include "utility/stringCompare.hpp"

#include "serverKernal.hpp"
#include "wifi/http.hpp"
#include "wifi/socketStreamWindow.hpp"
#include "wifi/wifi.hpp"

#include "buildinHtml/file.hpp"
#include "storage/fat.hpp"

#include "screenStream/screenStream.hpp"
#include "esp_heap_caps.h"
#include "virtualIndev/virtualIndev.hpp"

#include "task/task.hpp"

EXT_RAM_BSS_ATTR static uint8_t autoRestartTimes = 0;
EXT_RAM_BSS_ATTR static uint8_t maxRestartTimes = 0;

constexpr char TAG[] = "server";

constexpr char ServerPath[] = "/server";
constexpr char FormatingPassword[] = "I know exactly what I'm doing";
constexpr size_t FlashPutMaxSize = 6 * 1024 * 1024; //6M
constexpr size_t MemPutMaxSize = 6 * 1024 * 1024; //6M
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
void httpPut(IOSocketStream& socketStream, HttpRequest& request);
void httpDelete(IOSocketStream& socketStream, HttpRequest& request);
void restart();

bool apiScreenStream(IOSocketStream& socketStream);
void apiScreenTouch(IOSocketStream& socketStream, HttpRequest& request);
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

void httpGet(IOSocketStream& socketStream, HttpRequest& request)
{
	const char* uri = (!(request.getPath()[0] == '/' && request.getPath()[1] == '\0')) ? (request.getPath()) : ("/index.html");

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

	if (stringCompare((char*)uri, uriLenght, "/api/screen/stream", 18))
	{
		apiScreenStream(socketStream);
		return;
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/screen/touch", 17))
	{
		apiScreenTouch(socketStream, request);
		return;
	}
	else if (stringCompare((char*)uri, uriLenght, "/api/floor", 10))
	{
		size_t pathSize = sizeof(PerfixRoot) + request.getBodyLenght();
		char* path = new char[pathSize];
		strcpy(path, PerfixRoot);
		socketStream.read(path + sizeof(PerfixRoot) - 1, pathSize - sizeof(PerfixRoot) + 1);
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
			char ssid[33];
			char password[64];
			socketStream.getline(ssid, sizeof(ssid));
			socketStream.getline(password, sizeof(password));
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
			char ssid[33];
			char password[64];
			socketStream.getline(ssid, sizeof(ssid));
			socketStream.getline(password, sizeof(password));
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

		char body[request.bodyLenght + 1] = "";
		size_t bodyLength = socketStream.read(body, request.bodyLenght + 1);
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
	else
	{
		sendBadRequest(socketStream);
		return;
	}
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
	pathLength += socketStream.readByte(path + pathLength, request.getBodyLenght());
	path[pathLength++] = '\0';

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

bool apiScreenStream(IOSocketStream& socketStream)
{
	constexpr size_t bufferSize = 512 * 1024;
	uint8_t* out = (uint8_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_DMA);
	if (!out)
	{
		ESP_LOGI("/api/screen/stream", "alloc buf failed");
		return false;
	}

	// MJPEG stream over POST
	socketStream.write("HTTP/1.1 200 OK\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary=--FRAME\r\n"
		"\r\n", 78);
	socketStream.sendNow();

	while (socketStream.isGood() && serverRunning)
	{
		if (size_t jpegSize = ScreenStream::instance().captureJpeg(out, bufferSize))
		{
			char partHeader[128];
			size_t hlen = sprintf(partHeader, "--FRAME\r\n"
				"Content-Type: image/jpeg\r\n"
				"Content-Length: %u\r\n"
				"\r\n", (unsigned)jpegSize);

			socketStream.write(partHeader, hlen);
			socketStream.write((const char*)out, jpegSize);
			socketStream.write("\r\n", 2);
			socketStream.sendNow();
		}
		else vTaskDelay(100 / portTICK_PERIOD_MS);
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}

	heap_caps_free(out);
	return true;
}

void apiScreenTouch(IOSocketStream& socketStream, HttpRequest& request)
{
	size_t len = request.getBodyLenght();
	if (len == 0)
	{
		sendBadRequest(socketStream);
		return;
	}

	char* buf = new char[len + 1];
	size_t read = socketStream.read(buf, len + 1);
	buf[read < len ? read : len] = '\0';

	int x = 0, y = 0, t = 0;
	int parsed = sscanf(buf, "%d,%d,%d", &x, &y, &t);
	delete[] buf;

	if (parsed < 2)
	{
		sendBadRequest(socketStream);
		return;
	}

	lv_point_t pt{ (lv_coord_t)x, (lv_coord_t)y };
	lv_indev_state_t state = (t == 2) ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;

	VirtualIndev::instance().sendTouch(state, pt);

	sendOk(socketStream);
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
