#include "wsServer.hpp"
#include "virtualIndev/virtualIndev.hpp"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>

static constexpr char TAG[] = "wsServer";

EXT_RAM_BSS_ATTR static httpd_handle_t s_server = nullptr;

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
 *  启动 / 停止
 * ============================================================ */
bool wsServerStart()
{
	if (s_server) return true;

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = 8080;
	config.max_uri_handlers = 1;
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
	httpd_uri_t wsTouchUri{};
	wsTouchUri.uri = "/ws/touch";
	wsTouchUri.method = HTTP_GET;
	wsTouchUri.handler = wsTouchHandler;
	wsTouchUri.user_ctx = nullptr;
	wsTouchUri.is_websocket = true;
	wsTouchUri.handle_ws_control_frames = false;
	wsTouchUri.supported_subprotocol = nullptr;

	ret = httpd_register_uri_handler(s_server, &wsTouchUri);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "httpd_register_uri_handler failed: %d", ret);
		httpd_stop(s_server);
		s_server = nullptr;
		return false;
	}

	ESP_LOGI(TAG, "WebSocket server started on port 8080 (uri: /ws/touch)");
	return true;
}

void wsServerStop()
{
	if (s_server)
	{
		httpd_stop(s_server);
		s_server = nullptr;
		ESP_LOGI(TAG, "WebSocket server stopped");
	}
}
