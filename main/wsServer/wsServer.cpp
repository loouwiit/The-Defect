#include "wsServer.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "screenStream/screenStream.hpp"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>

static constexpr char TAG[] = "wsServer";

EXT_RAM_BSS_ATTR static httpd_handle_t s_server = nullptr;

static constexpr size_t kJpegBufSize = 512 * 1024;

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

	ESP_LOGI(TAG, "ws (%d, %d) %d", x, y, t);

	/* 注入到 LVGL */
	{
		lv_point_t pt{ (lv_coord_t)x, (lv_coord_t)y };
		lv_indev_state_t state = (t == 2) ? LV_INDEV_STATE_REL : LV_INDEV_STATE_PR;
		VirtualIndev::instance().sendTouch(state, pt);
	}

	return ESP_OK;
}

/* ============================================================
 *  串流 — 专用推送任务 (避免阻塞 httpd 单一线程)
 * ============================================================ */

 /* 串流客户端信息 */
typedef struct {
	httpd_handle_t hd;
	int            fd;
	volatile bool  active;
	TaskHandle_t   task;
} StreamClient;

EXT_RAM_BSS_ATTR static StreamClient s_streamClient = {};

/* 异步发送完成回调：释放帧缓冲区 */
static void streamFreeCb(esp_err_t err, int socket, void* arg)
{
	uint8_t* buf = (uint8_t*)arg;
	if (buf) heap_caps_free(buf);
	if (err != ESP_OK)
		ESP_LOGW(TAG, "stream send err=%d fd=%d", err, socket);
}

/* 专用推送任务：独立于 httpd 线程运行 */
static void streamPushTask(void* arg)
{
	StreamClient* cl = (StreamClient*)arg;
	ESP_LOGI(TAG, "stream push task started (fd=%d)", cl->fd);

	/* 分配一次 JPEG 缓冲区，推流期间复用 */
	uint8_t* jpegBuf = (uint8_t*)heap_caps_malloc(
		kJpegBufSize,
		MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_DMA);
	if (!jpegBuf)
	{
		ESP_LOGE(TAG, "jpegBuf alloc failed");
		cl->task = nullptr;
		vTaskDelete(nullptr);
		return;
	}

	while (cl->active && s_server)
	{
		/* 检测客户端是否还活着 */
		if (httpd_ws_get_fd_info(cl->hd, cl->fd) != HTTPD_WS_CLIENT_WEBSOCKET)
		{
			ESP_LOGI(TAG, "stream client disconnected");
			break;
		}

		/* 抓取一帧 JPEG */
		size_t jpegSize = ScreenStream::instance().captureJpeg(jpegBuf, kJpegBufSize);
		if (jpegSize == 0)
		{
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;
		}

		/* 为这一帧分配独立缓冲区（异步发送需要 payload 存活到回调） */
		uint8_t* frameBuf = (uint8_t*)heap_caps_malloc(jpegSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (!frameBuf)
		{
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;
		}
		memcpy(frameBuf, jpegBuf, jpegSize);

		/* 异步发送 */
		httpd_ws_frame_t frame;
		memset(&frame, 0, sizeof(frame));
		frame.type = HTTPD_WS_TYPE_BINARY;
		frame.payload = frameBuf;
		frame.len = jpegSize;

		esp_err_t ret = httpd_ws_send_data_async(cl->hd, cl->fd, &frame, streamFreeCb, frameBuf);
		if (ret != ESP_OK)
		{
			ESP_LOGW(TAG, "stream push err=%d", ret);
			heap_caps_free(frameBuf);
			break;
		}

		/* 帧率控制 ~30fps */
		vTaskDelay(pdMS_TO_TICKS(33));
	}

	cl->active = false;
	cl->task = nullptr;
	heap_caps_free(jpegBuf);
	ESP_LOGI(TAG, "stream push task ended");
	vTaskDelete(nullptr);
}

/* ============================================================
 *  WebSocket handler — 屏幕串流
 *  工作流:
 *    1. 握手阶段保存连接信息
 *    2. 客户端发任意 binary 触发启动推送任务
 *    3. handler 立即返回，httpd 线程不阻塞
 *    4. 推送任务独立运行，异步发帧
 * ============================================================ */
static esp_err_t wsStreamHandler(httpd_req_t* req)
{
	if (req->method == HTTP_GET)
	{
		/* 握手：保存连接信息 */
		if (s_streamClient.active)
		{
			ESP_LOGW(TAG, "only one stream client supported, reject");
			return ESP_FAIL;  // 拒绝第二个串流客户端
		}
		s_streamClient.hd = req->handle;
		s_streamClient.fd = httpd_req_to_sockfd(req);
		s_streamClient.active = false;
		s_streamClient.task = nullptr;
		ESP_LOGI(TAG, "ws/stream handshake done (fd=%d)", s_streamClient.fd);
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

	/* 启动推送任务（如果尚未启动） */
	if (s_streamClient.task == nullptr)
	{
		s_streamClient.active = true;
		BaseType_t ret = xTaskCreate(streamPushTask, "wsStream",
			4096, &s_streamClient,
			configMAX_PRIORITIES - 2, &s_streamClient.task);
		if (ret != pdPASS)
		{
			s_streamClient.active = false;
			ESP_LOGE(TAG, "stream task create failed");
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
	config.server_port = 8080;
	config.max_uri_handlers = 2;
	config.max_open_sockets = 4; // 最多 4 个 WS 客户端
	config.lru_purge_enable = true;

	esp_err_t ret = httpd_start(&s_server, &config);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "httpd_start failed: %d", ret);
		s_server = nullptr;
		return false;
	}

	/* 注册 WebSocket URI 处理器 */
	auto reg = [&](const char* uri, esp_err_t(*handler)(httpd_req_t*)) -> bool
		{
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
			return true;
		};

	if (!reg("/ws/touch", wsTouchHandler) || !reg("/ws/stream", wsStreamHandler))
	{
		httpd_stop(s_server);
		s_server = nullptr;
		return false;
	}

	ESP_LOGI(TAG, "WebSocket server started on port 8080");
	return true;
}

void wsServerStop()
{
	if (s_streamClient.active)
	{
		s_streamClient.active = false;
		/* 等待推送任务退出 */
		int wait = 50;
		while (s_streamClient.task && --wait)
			vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (s_server)
	{
		httpd_stop(s_server);
		s_server = nullptr;
		ESP_LOGI(TAG, "WebSocket server stopped");
	}
}
