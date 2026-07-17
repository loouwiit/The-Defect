#include "wsServer.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "screenStream/screenStream.hpp"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <task/task.hpp>
#include <sys/socket.h>
#include <errno.h>

static constexpr char TAG[] = "wsServer";

EXT_RAM_BSS_ATTR static httpd_handle_t s_server = nullptr;

static constexpr size_t kJpegBufSize = 512 * 1024;

/* 连续编码失败最大次数后放弃 */
static constexpr int kMaxConsecutiveErrors = 50;

/* ============================================================
 *  WebSocket handler — 接收触摸二进制帧
 *  协议: 12 字节二进制 (3×int32 小端序)
 *    [0..3]  x 坐标
 *    [4..7]  y 坐标
 *    [8..11] type: 0=down, 1=move, 2=up
 * ============================================================ */
static esp_err_t wsTouchHandler(httpd_req_t* req)
{
	/* --- 握手阶段 --- */
	if (req->method == HTTP_GET)
	{
		ESP_LOGI(TAG, "WebSocket handshake done");
		return ESP_OK;
	}

	/* --- 数据阶段: 接收 WebSocket 帧 --- */
	httpd_ws_frame_t wsPkt;
	memset(&wsPkt, 0, sizeof(wsPkt));
	wsPkt.type = HTTPD_WS_TYPE_BINARY;

	/* 第一步: 获取帧长度 */
	esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
	if (ret != ESP_OK)
	{
		ESP_LOGW(TAG, "httpd_ws_recv_frame (get len) failed: %d", ret);
		return ret;
	}

	/* 第二步: 只接受 12 字节的二进制帧 */
	if (wsPkt.len != 12)
	{
		if (wsPkt.len > 0)
		{
			/* 丢弃不合法帧 */
			uint8_t* discard = (uint8_t*)heap_caps_malloc(wsPkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if (discard)
			{
				wsPkt.payload = discard;
				httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
				heap_caps_free(discard);
			}
		}
		return ESP_OK;
	}

	/* 分配 12 字节缓冲区 (PSRAM) */
	uint8_t* buf = (uint8_t*)heap_caps_malloc(12, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!buf)
	{
		ESP_LOGE(TAG, "OOM");
		return ESP_ERR_NO_MEM;
	}

	wsPkt.payload = buf;
	ret = httpd_ws_recv_frame(req, &wsPkt, 12);
	if (ret != ESP_OK)
	{
		ESP_LOGW(TAG, "httpd_ws_recv_frame failed: %d", ret);
		heap_caps_free(buf);
		return ret;
	}

	/* 解析 3×int32 小端序 */
	int32_t x = (int32_t)(buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
	int32_t y = (int32_t)(buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24));
	int32_t t = (int32_t)(buf[8] | (buf[9] << 8) | (buf[10] << 16) | (buf[11] << 24));

	heap_caps_free(buf);

	/* 注入到 LVGL */
	{
		lv_point_t pt{ (lv_coord_t)x, (lv_coord_t)y };
		lv_indev_state_t state = (t == 2) ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;
		VirtualIndev::instance().sendTouch(state, pt);
	}

	return ESP_OK;
}

/* ============================================================
 *  串流 — 双任务并行：编码任务 + 发送任务
 *  编码任务占用 JPEG 硬件，发送任务直接 send() 发 socket，
 *  两者通过队列传递 buffer，编码与发送真正并行。
 * ============================================================ */

/* 双缓冲 + 任务间通信 */
EXT_RAM_BSS_ATTR static uint8_t* s_jpegBufs[2] = {};

struct SendJob {
	uint8_t* data;
	size_t   size;
};

EXT_RAM_BSS_ATTR static TaskHandle_t s_encodeTask = nullptr;
EXT_RAM_BSS_ATTR static TaskHandle_t s_sendTask = nullptr;
EXT_RAM_BSS_ATTR static QueueHandle_t s_sendQueue = nullptr;
EXT_RAM_BSS_ATTR static SemaphoreHandle_t s_freeSem = nullptr;

EXT_RAM_BSS_ATTR static int s_streamFd = -1;
EXT_RAM_BSS_ATTR static httpd_handle_t s_streamHd = nullptr;
EXT_RAM_BSS_ATTR static volatile bool s_streamActive = false;

/* pending 连接（用于客户端切换） */
EXT_RAM_BSS_ATTR static httpd_handle_t s_pendingHd = nullptr;
EXT_RAM_BSS_ATTR static int s_pendingFd = -1;
EXT_RAM_BSS_ATTR static volatile bool s_hasPending = false;

/* 编码任务：捕获 JPEG → 入队 */
static void streamEncodeTask(void* arg)
{
	ESP_LOGI(TAG, "encode task started");

	while (s_streamActive && s_server)
	{
		if (xSemaphoreTake(s_freeSem, pdMS_TO_TICKS(100)) != pdTRUE)
			continue;
		if (!s_streamActive || !s_server) { xSemaphoreGive(s_freeSem); break; }

		static int encodeBufIdx = 0;
		int idx = encodeBufIdx;
		encodeBufIdx ^= 1;

		size_t jpegSize = ScreenStream::instance().captureJpeg(
			s_jpegBufs[idx], kJpegBufSize);
		if (jpegSize == 0)
		{
			xSemaphoreGive(s_freeSem);
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;
		}

		SendJob job = { s_jpegBufs[idx], jpegSize };
		if (xQueueSend(s_sendQueue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
			xSemaphoreGive(s_freeSem);
	}

	ESP_LOGI(TAG, "encode task ended");
	vTaskDelete(nullptr);
}

/* 发送任务：出队 → 构造 WS 帧头 → send() */
static void streamSendTask(void* arg)
{
	ESP_LOGI(TAG, "send task started");
	int consecutiveErrors = 0;

	while (s_streamActive && s_server)
	{
		SendJob job;
		if (xQueueReceive(s_sendQueue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
			continue;
		if (!s_streamActive || !s_server) { xSemaphoreGive(s_freeSem); break; }

		if (httpd_ws_get_fd_info(s_streamHd, s_streamFd) != HTTPD_WS_CLIENT_WEBSOCKET)
		{
			ESP_LOGI(TAG, "stream client disconnected");
			xSemaphoreGive(s_freeSem);
			break;
		}

		uint8_t header[10];
		header[0] = 0x82;
		size_t headerSize;
		uint64_t n = job.size;
		if (n < 126) {
			header[1] = (uint8_t)n; headerSize = 2;
		} else if (n < 65536) {
			header[1] = 126;
			header[2] = (uint8_t)(n >> 8); header[3] = (uint8_t)n;
			headerSize = 4;
		} else {
			header[1] = 127;
			header[2] = (uint8_t)(n >> 56); header[3] = (uint8_t)(n >> 48);
			header[4] = (uint8_t)(n >> 40); header[5] = (uint8_t)(n >> 32);
			header[6] = (uint8_t)(n >> 24); header[7] = (uint8_t)(n >> 16);
			header[8] = (uint8_t)(n >> 8);  header[9] = (uint8_t)n;
			headerSize = 10;
		}

		ssize_t sent = send(s_streamFd, header, headerSize, 0);
		if (sent <= 0 || (size_t)sent != headerSize) {
			consecutiveErrors++;
			if (consecutiveErrors >= kMaxConsecutiveErrors) {
				xSemaphoreGive(s_freeSem); break;
			}
			xSemaphoreGive(s_freeSem);
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;
		}
		sent = send(s_streamFd, job.data, job.size, 0);
		if (sent <= 0 || (size_t)sent != job.size) {
			consecutiveErrors++;
			if (consecutiveErrors >= kMaxConsecutiveErrors) {
				xSemaphoreGive(s_freeSem); break;
			}
			xSemaphoreGive(s_freeSem);
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;
		}
		consecutiveErrors = 0;

		xSemaphoreGive(s_freeSem);
	}

	ESP_LOGI(TAG, "send task ended");
	vTaskDelete(nullptr);
}

/* 启动双任务 */
static bool startStreamTasks()
{
	const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_DMA;
	if (!s_jpegBufs[0]) s_jpegBufs[0] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
	if (!s_jpegBufs[1]) s_jpegBufs[1] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
	if (!s_jpegBufs[0] || !s_jpegBufs[1]) return false;

	if (!s_freeSem)  s_freeSem  = xSemaphoreCreateCounting(2, 2);
	if (!s_sendQueue) s_sendQueue = xQueueCreate(2, sizeof(SendJob));
	if (!s_freeSem || !s_sendQueue) return false;

	s_streamActive = true;

	if (xTaskCreate(streamEncodeTask, "wsEnc", 4096, nullptr,
			Task::Priority::Deamon, &s_encodeTask) != pdPASS)
		{ s_streamActive = false; return false; }

	if (xTaskCreate(streamSendTask, "wsSend", 4096, nullptr,
			Task::Priority::Deamon, &s_sendTask) != pdPASS)
		{ s_streamActive = false; return false; }

	return true;
}

/* 停止串流 */
static void stopStreamTasks()
{
	s_streamActive = false;
	int wait = 150;
	while ((s_encodeTask || s_sendTask) && --wait)
		vTaskDelay(pdMS_TO_TICKS(10));
	s_encodeTask = s_sendTask = nullptr;
	/* 清空队列中残留的 job，归还 permit */
	SendJob dummy;
	while (s_sendQueue && xQueueReceive(s_sendQueue, &dummy, 0) == pdTRUE)
		xSemaphoreGive(s_freeSem);
}

/* 异步踢掉当前串流客户端 */
static void kickOldStreamClientAsync()
{
	if (!s_streamActive && !s_encodeTask && !s_sendTask)
		return;
	ESP_LOGI(TAG, "kicking old stream client (fd=%d)", s_streamFd);
	s_streamActive = false;
}

/* 接管 pending 连接 */
static void adoptPendingStreamClient()
{
	if (!s_hasPending) return;
	s_streamHd = s_pendingHd;
	s_streamFd = s_pendingFd;
	s_hasPending = false;
	s_streamActive = true;
	ESP_LOGI(TAG, "adopted pending stream client (fd=%d)", s_streamFd);
}

/* ============================================================
 *  WebSocket handler — 屏幕串流
 *  工作流:
 *    1. 握手阶段保存连接信息
 *    2. 客户端发任意 binary 触发启动双任务
 *    3. handler 立即返回，httpd 线程不阻塞
 *    4. 编码+发送双任务并行运行
 * ============================================================ */
static esp_err_t wsStreamHandler(httpd_req_t* req)
{
	if (req->method == HTTP_GET)
	{
		if (s_streamActive || s_encodeTask || s_sendTask)
		{
			ESP_LOGW(TAG, "old stream client present, queuing new as pending");
			kickOldStreamClientAsync();
			s_pendingHd = req->handle;
			s_pendingFd = httpd_req_to_sockfd(req);
			s_hasPending = true;
		}
		else
		{
			s_streamHd = req->handle;
			s_streamFd = httpd_req_to_sockfd(req);
			s_streamActive = false;
			s_hasPending = false;
		}
		ESP_LOGI(TAG, "ws/stream handshake done (fd=%d)", httpd_req_to_sockfd(req));
		return ESP_OK;
	}

	/* 丢弃客户端请求帧 */
	{
		httpd_ws_frame_t pkt;
		memset(&pkt, 0, sizeof(pkt));
		esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
		if (ret != ESP_OK) return ret;
		if (pkt.len > 0)
		{
			uint8_t* tmp = (uint8_t*)heap_caps_malloc(pkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
			if (tmp)
			{
				pkt.payload = tmp;
				httpd_ws_recv_frame(req, &pkt, pkt.len);
				heap_caps_free(tmp);
			}
		}
	}

	/* 启动双任务（空闲时启动，或 pending 接管后启动） */
	if (!s_encodeTask && !s_sendTask)
	{
		if (s_hasPending) adoptPendingStreamClient();

		if (!startStreamTasks())
		{
			s_streamActive = false;
			ESP_LOGE(TAG, "start stream tasks failed");
		}
	}

	return ESP_OK;
}

/* ============================================================
 *  启动 / 停止
 * ============================================================ */
bool wsServerStart()
{
	if (s_server) return true;

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.task_priority = Task::Priority::RealTime;
	config.server_port = 8080;
	config.max_uri_handlers = 8;
	config.max_open_sockets = 8;
	config.lru_purge_enable = true;

	esp_err_t ret = httpd_start(&s_server, &config);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "httpd_start failed: %d", ret);
		s_server = nullptr;
		return false;
	}

	/* 注册内置 WebSocket URI 处理器 */
	if (!wsServerRegisterWs("/ws/touch", wsTouchHandler)
		|| !wsServerRegisterWs("/ws/stream", wsStreamHandler))
	{
		httpd_stop(s_server);
		s_server = nullptr;
		return false;
	}

	ESP_LOGI(TAG, "WebSocket server started on port 8080");
	return true;
}

// ============================================================
//  注册 / 注销（公开 API）
// ============================================================

bool wsServerRegisterWs(const char* uri, WsHandlerFn handler)
{
	if (!s_server)
	{
		ESP_LOGE(TAG, "server not started, cannot register %s", uri);
		return false;
	}

	httpd_uri_t u{};
	u.uri = uri;
	u.method = HTTP_GET;
	u.handler = handler;
	u.is_websocket = true;
	u.handle_ws_control_frames = false;

	if (httpd_register_uri_handler(s_server, &u) != ESP_OK)
	{
		ESP_LOGE(TAG, "register %s failed", uri);
		return false;
	}

	ESP_LOGI(TAG, "registered WebSocket handler: %s", uri);
	return true;
}

void wsServerUnregister(const char* uri)
{
	if (!s_server) return;

	httpd_unregister_uri_handler(s_server, uri, HTTP_GET);
	ESP_LOGI(TAG, "unregistered WebSocket handler: %s", uri);
}

// ============================================================
//  消息发送
// ============================================================

/** 异步发送完成回调：释放 arg 指向的 heap 缓冲区 */
static void sendCompleteCb(esp_err_t err, int socket, void* arg)
{
	if (arg) heap_caps_free(arg);
}

esp_err_t wsServerSendText(int fd, const char* data, size_t len)
{
	if (!s_server) return ESP_ERR_INVALID_STATE;

	/* 拷贝到 PSRAM，异步发送完成后在回调中释放 */
	char* copy = (char*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!copy) return ESP_ERR_NO_MEM;
	memcpy(copy, data, len);

	httpd_ws_frame_t frame{};
	frame.type = HTTPD_WS_TYPE_TEXT;
	frame.payload = (uint8_t*)copy;
	frame.len = len;

	esp_err_t ret = httpd_ws_send_data_async(s_server, fd, &frame, sendCompleteCb, copy);
	if (ret != ESP_OK) {
		heap_caps_free(copy);  // 异步不会执行，立即释放
	}
	return ret;
}

esp_err_t wsServerBroadcastText(const char* data, size_t len)
{
	if (!s_server) return ESP_ERR_INVALID_STATE;

	/* 获取所有客户端 fd */
	size_t maxFds = 8;
	int* fds = (int*)heap_caps_malloc(maxFds * sizeof(int), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!fds) return ESP_ERR_NO_MEM;

	esp_err_t ret = httpd_get_client_list(s_server, &maxFds, fds);
	if (ret != ESP_OK)
	{
		heap_caps_free(fds);
		return ret;
	}

	/* 遍历所有 fd，只发给 WebSocket 客户端 */
	for (size_t i = 0; i < maxFds; i++)
	{
		int fd = fds[i];
		if (fd < 0) continue;

		auto info = httpd_ws_get_fd_info(s_server, fd);
		if (info == HTTPD_WS_CLIENT_WEBSOCKET)
		{
			wsServerSendText(fd, data, len);
		}
	}

	heap_caps_free(fds);
	return ESP_OK;
}

void wsServerStop()
{
	stopStreamTasks();

	if (s_server)
	{
		httpd_stop(s_server);
		s_server = nullptr;
		ESP_LOGI(TAG, "WebSocket server stopped");
	}
}
