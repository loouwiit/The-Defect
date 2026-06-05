#include "wsServer.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "screenStream/screenStream.hpp"

#include "mutex/mutex.hpp"
#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <task/task.hpp>

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
	volatile bool  keepBuffers;       // 退出时是否保留 jpegBufs（true=新客户端将复用）

	/* 异步踢掉：握手阶段不阻塞 httpd，把新连接信息存为 pending，
	 * 旧推流任务退出时（或下一次 binary 触发时）再接管。 */
	httpd_handle_t pendingHd;
	int            pendingFd;
	volatile bool  hasPending;        // 是否有等待接管的新连接

	/* 双缓冲 ping-pong：严格 in-flight 跟踪
	 * 规则：写 jpegBufs[i] 前必须等待 inFlight[i] == 0（httpd 已读完）。
	 * 这强制生产者等消费者，防止 PSRAM buffer 被覆盖。 */
	uint8_t*       jpegBufs[2];
	int            bufIdx;            // 下一个要写入的 buffer 下标 (0/1)
	bool           inFlight[2];       // 每个 buffer 的异步发送是否未完成（inFlightMtx 保护）
	Mutex          inFlightMtx;       // 保护 inFlight[0/1] 的互斥量
} StreamClient;

EXT_RAM_BSS_ATTR static StreamClient s_streamClient = {};

/* 异步发送完成回调：标记对应 buffer 空闲。
 * arg 是被发送 buffer 在 jpegBufs[] 中的下标（0 或 1）。
 * buffer 本身由 streamPushTask 统一释放，本回调不 free。 */
static void streamFreeCb(esp_err_t err, int socket, void* arg)
{
	int idx = (int)(intptr_t)arg;  // buffer 下标

	if (err != ESP_OK)
		ESP_LOGW(TAG, "stream send err=%d fd=%d buf=%d", err, socket, idx);

	Lock lock{ s_streamClient.inFlightMtx };
	if (idx >= 0 && idx < 2)
		s_streamClient.inFlight[idx] = false;
}

/* 异步踢掉当前串流客户端：仅设标志，立即返回（不阻塞 httpd 工作线程）。
 * 推流任务会在下一次循环检测到 active=false 后主动退出并清理。 */
static void kickOldStreamClientAsync()
{
	if (!s_streamClient.active && !s_streamClient.task)
		return;

	ESP_LOGI(TAG, "kicking old stream client (fd=%d) async", s_streamClient.fd);

	/* 标记保留 buffer：旧任务退出时不会释放 jpegBufs，新任务复用 */
	s_streamClient.keepBuffers = true;

	/* 通知推流任务退出（异步，自己会在排空 in-flight 后退出） */
	s_streamClient.active = false;
}

/* 接管 pending 连接：把暂存的新连接信息安装为当前活动客户端。
 * 在推流任务退出排空后、或 binary 触发时调用。 */
static void adoptPendingStreamClient()
{
	if (!s_streamClient.hasPending)
		return;

	s_streamClient.hd = s_streamClient.pendingHd;
	s_streamClient.fd = s_streamClient.pendingFd;
	s_streamClient.hasPending = false;
	s_streamClient.active = true;
	s_streamClient.task = nullptr;  // 重新创建
	s_streamClient.bufIdx = 0;
	/* 旧任务退出时已 drain 完毕，inFlight[] 一定为 false，但仍显式重置 */
	{
		Lock lock{ s_streamClient.inFlightMtx };
		s_streamClient.inFlight[0] = false;
		s_streamClient.inFlight[1] = false;
	}
	ESP_LOGI(TAG, "adopted pending stream client (fd=%d)", s_streamClient.fd);
}

/* 专用推送任务：独立于 httpd 线程运行
 * 双缓冲方案：两个 512KB buffer 轮流写入和发送，零 memcpy。
 *   buffer A: jpeg_enc 写入 → httpd 异步发送
 *   buffer B: 同时 jpeg_enc 写入 → httpd 异步发送
 * 任意时刻同一 buffer 仅被一方使用。
 *
 * 健壮性设计：
 * - 单次错误不退出，连续错误超过阈值才放弃
 * - 重试机制：等一帧再试 */
static void streamPushTask(void* arg)
{
	StreamClient* cl = (StreamClient*)arg;
	ESP_LOGI(TAG, "stream push task started (fd=%d)", cl->fd);

	/* 分配两个 JPEG 缓冲区（双缓冲 ping-pong） */
	const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_DMA;
	cl->jpegBufs[0] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
	cl->jpegBufs[1] = (uint8_t*)heap_caps_malloc(kJpegBufSize, caps);
	if (!cl->jpegBufs[0] || !cl->jpegBufs[1])
	{
		ESP_LOGE(TAG, "jpegBufs alloc failed");
		heap_caps_free(cl->jpegBufs[0]);
		heap_caps_free(cl->jpegBufs[1]);
		cl->jpegBufs[0] = cl->jpegBufs[1] = nullptr;
		cl->active = false;
		cl->task = nullptr;
		vTaskDelete(nullptr);
		return;
	}

	cl->bufIdx = 0;
	cl->inFlight[0] = false;
	cl->inFlight[1] = false;

	int consecutiveErrors = 0;

	while (cl->active && s_server)
	{
		/* 检测客户端是否还活着 */
		if (httpd_ws_get_fd_info(cl->hd, cl->fd) != HTTPD_WS_CLIENT_WEBSOCKET)
		{
			ESP_LOGI(TAG, "stream client disconnected");
			break;
		}

		/* 背压等待：当前 buffer 若还在异步发送中（in-flight），则等待回调完成。
		 * 这是天然限流——消费者（WiFi/浏览器）多慢，生产者就多慢。
		 * 不限时，不主动断流。 */
		while (true)
		{
			{
				Lock lock{ cl->inFlightMtx };
				if (!cl->inFlight[cl->bufIdx])
					break;
			}
			if (!cl->active || !s_server) break;
			vTaskDelay(pdMS_TO_TICKS(5));
		}

		/* 选当前可写的 buffer */
		uint8_t* curBuf = cl->jpegBufs[cl->bufIdx];

		/* 抓取一帧 JPEG（jpeg_enc_process 是同步等待硬件完成，可能因总线竞争延迟） */
		size_t jpegSize = ScreenStream::instance().captureJpeg(curBuf, kJpegBufSize);
		if (jpegSize == 0)
		{
			consecutiveErrors++;
			if (consecutiveErrors >= kMaxConsecutiveErrors)
			{
				ESP_LOGE(TAG, "too many consecutive errors (%d), giving up", consecutiveErrors);
				break;
			}
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;
		}
		consecutiveErrors = 0;  // 成功后重置

		/* 标记当前 buffer 为 in-flight（必须在 send_data_async 之前） */
		{
			Lock lock{ cl->inFlightMtx };
			cl->inFlight[cl->bufIdx] = true;
		}

		/* 异步发送：payload 直接指向双缓冲当前 buffer，无 memcpy。
		 * 回调中通过 arg 知道是哪个 buffer，置 inFlight[idx]=false。 */
		httpd_ws_frame_t frame{};
		frame.type = HTTPD_WS_TYPE_BINARY;
		frame.payload = curBuf;
		frame.len = jpegSize;

		int curIdx = cl->bufIdx;  // 捕获，避免与 bufIdx ^= 1 竞争
		esp_err_t ret = httpd_ws_send_data_async(cl->hd, cl->fd, &frame,
			streamFreeCb, (void*)(intptr_t)curIdx);
		if (ret != ESP_OK)
		{
			ESP_LOGW(TAG, "stream push err=%d (consecutive=%d)", ret, consecutiveErrors);
			/* 发送失败，回滚 in-flight 标志 */
			{
				Lock lock{ cl->inFlightMtx };
				cl->inFlight[curIdx] = false;
			}
			consecutiveErrors++;
			if (consecutiveErrors >= kMaxConsecutiveErrors)
			{
				ESP_LOGE(TAG, "too many send errors, giving up");
				break;
			}
			vTaskDelay(pdMS_TO_TICKS(33));
			continue;  /* 重试，不退出 */
		}
		consecutiveErrors = 0;

		/* 切换到另一个 buffer（ping-pong） */
		cl->bufIdx ^= 1;

		/* 帧率控制 ~20fps（实际节奏受 in-flight 等待自适应） */
		vTaskDelay(pdMS_TO_TICKS(50));
	}

	cl->active = false;

	/* 等待所有 in-flight 异步发送完成，确保 httpd 不再读取 buffer */
	while (true)
	{
		{
			Lock lock{ cl->inFlightMtx };
			if (!cl->inFlight[0] && !cl->inFlight[1])
				break;
		}
		if (!s_server) break;  // 服务器关闭时强制退出
		vTaskDelay(pdMS_TO_TICKS(5));
	}

	cl->task = nullptr;

	/* keepBuffers=true：被新客户端踢掉，buffer 保留供下一位复用（零分配）
	 * keepBuffers=false：服务器关闭，安全释放 */
	if (!cl->keepBuffers)
	{
		heap_caps_free(cl->jpegBufs[0]);
		heap_caps_free(cl->jpegBufs[1]);
		cl->jpegBufs[0] = cl->jpegBufs[1] = nullptr;
	}
	else
	{
		/* keepBuffers=true 时旧 buffer 保留，但新任务 alloc 时会漏掉旧指针。
		 * 这里置 nullptr 避免新任务误释放旧 buffer */
		cl->jpegBufs[0] = nullptr;
		cl->jpegBufs[1] = nullptr;
	}
	cl->keepBuffers = false;

	/* 异步接管：若握手阶段有 pending 新连接，本任务退出时自动启动新的推流任务 */
	if (cl->hasPending)
	{
		adoptPendingStreamClient();
		BaseType_t ret = xTaskCreate(streamPushTask, "wsStream",
			8192, cl,
			Task::Priority::Deamon, &cl->task);
		if (ret != pdPASS)
		{
			cl->active = false;
			ESP_LOGE(TAG, "adopted stream task create failed");
		}
	}

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
		/* 握手：永不阻塞 httpd 线程。
		 * 情况 A：当前空闲 → 直接安装连接信息。
		 * 情况 B：当前有客户端 → 异步踢掉（旧任务退出时会自动接管 pending），
		 *         本次握手把新连接存为 pending，等旧任务退出后接管。 */
		if (s_streamClient.active || s_streamClient.task)
		{
			ESP_LOGW(TAG, "old stream client present, queuing new as pending");
			kickOldStreamClientAsync();
			s_streamClient.pendingHd = req->handle;
			s_streamClient.pendingFd = httpd_req_to_sockfd(req);
			s_streamClient.hasPending = true;
		}
		else
		{
			s_streamClient.hd = req->handle;
			s_streamClient.fd = httpd_req_to_sockfd(req);
			s_streamClient.active = false;
			s_streamClient.task = nullptr;
			s_streamClient.bufIdx = 0;
			s_streamClient.hasPending = false;
			{
				Lock lock{ s_streamClient.inFlightMtx };
				s_streamClient.inFlight[0] = false;
				s_streamClient.inFlight[1] = false;
			}
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

	/* 启动推送任务
	 * 情况 A: 当前空闲（task==nullptr, 无 pending） → 立即启动
	 * 情况 B: 有 pending 且 task 还在运行 → 等旧任务退出时接管
	 * 情况 C: 有 pending 但 task 已退出（异常） → 直接接管并启动
	 * 情况 D: task 正在运行 → 无需操作 */
	if (s_streamClient.task == nullptr)
	{
		if (s_streamClient.hasPending)
		{
			/* 异常恢复：task 已退但 pending 没被接管 */
			ESP_LOGW(TAG, "adopting stale pending client (task died)");
			adoptPendingStreamClient();
		}
		else
		{
			/* 正常启动 */
		}

		s_streamClient.active = true;
		BaseType_t ret = xTaskCreate(streamPushTask, "wsStream",
			8192, &s_streamClient,
			Task::Priority::Deamon, &s_streamClient.task);
		if (ret != pdPASS)
		{
			s_streamClient.active = false;
			ESP_LOGE(TAG, "stream task create failed");
		}
	}
	else if (s_streamClient.hasPending)
	{
		/* pending 已存在但旧任务还在退：本次 binary 触发只是确认旧客户端已断，
		 * 不需要做任何事——adoptPendingStreamClient() 会在旧任务退出时执行。 */
		ESP_LOGI(TAG, "trigger while pending, waiting for old task to exit");
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
	if (s_streamClient.active || s_streamClient.task)
	{
		/* 关闭服务器时 keepBuffers 保持默认 false，buffer 会被释放 */
		s_streamClient.active = false;
		/* 等待推送任务退出（任务内部还会额外等 in-flight 排空，最多 1s） */
		int wait = 150;
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
