/**
 * @file wsServer.cpp
 * @brief WebSocket 服务器核心 — 纯生命周期 + 注册 + 消息发送
 */

#include "wsServer.hpp"

#include <esp_log.h>
#include <esp_http_server.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <unistd.h>

#include "screenStream/wsStreamHandler.hpp"

static constexpr char TAG[] = "wsServer";

EXT_RAM_BSS_ATTR static httpd_handle_t s_server = nullptr;

/* ============================================================
 *  启动 / 停止
 * ============================================================ */

bool wsServerStart()
{
	if (s_server) return true;

	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.task_priority = 8; // RealTime
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

	ESP_LOGI(TAG, "WebSocket server started on port 8080");
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

// ============================================================
//  注册 / 注销
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

static void sendCompleteCb(esp_err_t err, int socket, void* arg)
{
	if (arg) heap_caps_free(arg);
}

esp_err_t wsServerSendText(int fd, const char* data, size_t len)
{
	if (!s_server) return ESP_ERR_INVALID_STATE;

	char* copy = (char*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!copy) return ESP_ERR_NO_MEM;
	memcpy(copy, data, len);

	httpd_ws_frame_t frame{};
	frame.type = HTTPD_WS_TYPE_TEXT;
	frame.payload = (uint8_t*)copy;
	frame.len = len;

	esp_err_t ret = httpd_ws_send_data_async(s_server, fd, &frame, sendCompleteCb, copy);
	if (ret != ESP_OK) {
		heap_caps_free(copy);
	}
	return ret;
}

esp_err_t wsServerBroadcastText(const char* data, size_t len)
{
	if (!s_server) return ESP_ERR_INVALID_STATE;

	size_t maxFds = 8;
	int* fds = (int*)heap_caps_malloc(maxFds * sizeof(int), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!fds) return ESP_ERR_NO_MEM;

	esp_err_t ret = httpd_get_client_list(s_server, &maxFds, fds);
	if (ret != ESP_OK)
	{
		heap_caps_free(fds);
		return ret;
	}

	for (size_t i = 0; i < maxFds; i++)
	{
		int fd = fds[i];
		if (fd < 0) continue;

		auto info = httpd_ws_get_fd_info(s_server, fd);
		if (info == HTTPD_WS_CLIENT_WEBSOCKET)
		{
			/* 串流 fd 由 streamSendTask 独占写，跳过避免并发写帧交错 */
			if (wsStreamIsFd(fd)) continue;
			wsServerSendText(fd, data, len);
		}
	}

	heap_caps_free(fds);
	return ESP_OK;
}

void wsServerCloseClient(int fd)
{
	if (!s_server || fd < 0) return;
	httpd_sess_trigger_close(s_server, fd);
	ESP_LOGI(TAG, "closed client fd=%d", fd);
}


