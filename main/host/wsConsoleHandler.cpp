/**
 * @file wsConsoleHandler.cpp
 * @brief /ws/console — 设备管理通道
 */

#include "wsConsoleHandler.hpp"
#include "inputHost.hpp"
#include "hostManager.hpp"
#include "websocket/wsServer.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <cJSON.h>

static constexpr char TAG[] = "WSConsole";

// ── 已连接的客户端列表（用于广播）──
static constexpr int MAX_CONSOLE_CLIENTS = 8;
static struct {
	int fd;
	int hostId;  // 该客户端分配的 hostId (1-3)
} s_clients[MAX_CONSOLE_CLIENTS] = {};
static int s_clientCount = 0;

static void addClient(int fd, int hostId)
{
	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++) {
		if (s_clients[i].fd <= 0) {
			s_clients[i] = { fd, hostId };
			if (i >= s_clientCount) s_clientCount = i + 1;
			return;
		}
	}
}

static void removeClient(int fd)
{
	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++) {
		if (s_clients[i].fd == fd) {
			s_clients[i] = { -1, -1 };
			return;
		}
	}
}

static int findHostIdByFd(int fd)
{
	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++) {
		if (s_clients[i].fd == fd) return s_clients[i].hostId;
	}
	return -1;
}

/// 广播 JSON 文本帧到所有已连接的控制台客户端（除发送者外）
static void broadcastExcept(const char* json, size_t len, int excludeFd)
{
	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++) {
		if (s_clients[i].fd > 0 && s_clients[i].fd != excludeFd) {
			wsServerSendText(s_clients[i].fd, json, len);
		}
	}
}

/// 广播到所有客户端
static void broadcastAll(const char* json, size_t len) {
	broadcastExcept(json, len, -1);
}

// ── JSON 辅助 ──
static char* makeJson(const char* type) {
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", type);
	char* s = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	return s;
}

// ════════════════════════════════════════════════════════════════
//  Handler
// ════════════════════════════════════════════════════════════════

esp_err_t wsConsoleHandler(httpd_req_t* req)
{
	/* ── 握手 ── */
	if (req->method == HTTP_GET) {
		ESP_LOGI(TAG, "WebSocket handshake /ws/console");
		return ESP_OK;
	}

	/* ── 接收帧 ── */
	httpd_ws_frame_t wsPkt{};
	wsPkt.type = HTTPD_WS_TYPE_TEXT;

	esp_err_t ret = httpd_ws_recv_frame(req, &wsPkt, 0);
	if (ret != ESP_OK) {
		int fd = httpd_req_to_sockfd(req);
		int hostId = findHostIdByFd(fd);

		if (hostId > 0) {
			// 清理 InputHost
			InputHost::instance().unregisterClient(fd);
			// 通知 HostManager
			HostManager::instance().onClientDisconnected(hostId);
			// 广播离开
			char* msg = makeJson("client_left");
			cJSON* root = cJSON_Parse(msg);
			cJSON_AddNumberToObject(root, "slot", hostId);
			char* updated = cJSON_PrintUnformatted(root);
			cJSON_Delete(root);
			cJSON_free(msg);
			broadcastAll(updated, strlen(updated));
			cJSON_free(updated);
			ESP_LOGI(TAG, "hostId=%d disconnected (fd=%d)", hostId, fd);
		}
		removeClient(fd);
		return ret;
	}

	if (wsPkt.len == 0) return ESP_OK;
	if (wsPkt.len > 1024) {
		uint8_t* d = (uint8_t*)heap_caps_malloc(wsPkt.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		if (d) { wsPkt.payload = d; httpd_ws_recv_frame(req, &wsPkt, wsPkt.len); heap_caps_free(d); }
		return ESP_OK;
	}

	char* buf = (char*)heap_caps_malloc(wsPkt.len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!buf) return ESP_ERR_NO_MEM;
	wsPkt.payload = (uint8_t*)buf;
	ret = httpd_ws_recv_frame(req, &wsPkt, wsPkt.len);
	if (ret != ESP_OK) { heap_caps_free(buf); return ret; }
	buf[wsPkt.len] = '\0';

	/* ── 解析 ── */
	cJSON* root = cJSON_Parse(buf);
	heap_caps_free(buf);
	if (!root) { cJSON_Delete(root); return ESP_OK; }

	cJSON* typeItem = cJSON_GetObjectItem(root, "type");
	if (!typeItem || !cJSON_IsString(typeItem)) { cJSON_Delete(root); return ESP_OK; }
	const char* type = typeItem->valuestring;
	int fd = httpd_req_to_sockfd(req);

	if (strcmp(type, "register") == 0) {
		cJSON* hn = cJSON_GetObjectItem(root, "hostname");
		cJSON* gp = cJSON_GetObjectItem(root, "gamepad_count");
		const char* hostname = hn && cJSON_IsString(hn) ? hn->valuestring : "unknown";
		int gamepadCount = gp && cJSON_IsNumber(gp) ? gp->valueint : 0;

		// 先检查是否已注册（重连场景）
		int existingHostId = findHostIdByFd(fd);
		if (existingHostId > 0) {
			// 已注册，更新信息
			HostManager::instance().onClientRegistered(
				existingHostId, hostname, "", gamepadCount);
			cJSON_Delete(root);
			return ESP_OK;
		}

		// 分配 hostId
		int hostId = InputHost::instance().registerClient(fd, hostname);
		if (hostId < 0) {
			ESP_LOGW(TAG, "no free slot for %s", hostname);
			cJSON_Delete(root);
			return ESP_OK;
		}

		addClient(fd, hostId);

		// 回复 registered
		cJSON* ack = cJSON_CreateObject();
		cJSON_AddStringToObject(ack, "type", "registered");
		cJSON_AddNumberToObject(ack, "slot", hostId);
		char* ackStr = cJSON_PrintUnformatted(ack);
		cJSON_Delete(ack);
		wsServerSendText(fd, ackStr, strlen(ackStr));
		cJSON_free(ackStr);

		// 更新 HostManager
		HostManager::instance().onClientRegistered(
			hostId, hostname, "", gamepadCount);

		// 广播 joined
		cJSON* joined = cJSON_CreateObject();
		cJSON_AddStringToObject(joined, "type", "client_joined");
		cJSON_AddNumberToObject(joined, "slot", hostId);
		cJSON_AddStringToObject(joined, "hostname", hostname);
		cJSON_AddNumberToObject(joined, "gamepad_count", gamepadCount);
		char* joinedStr = cJSON_PrintUnformatted(joined);
		cJSON_Delete(joined);
		broadcastAll(joinedStr, strlen(joinedStr));
		cJSON_free(joinedStr);

		ESP_LOGI(TAG, "%s registered as hostId=%d (fd=%d, %d gamepads)",
				 hostname, hostId, fd, gamepadCount);
	}
	else if (strcmp(type, "ping") == 0) {
		cJSON* pong = cJSON_CreateObject();
		cJSON_AddStringToObject(pong, "type", "pong");
		char* s = cJSON_PrintUnformatted(pong);
		cJSON_Delete(pong);
		wsServerSendText(fd, s, strlen(s));
		cJSON_free(s);
	}
	else if (strcmp(type, "slot_swap") == 0) {
		cJSON* a = cJSON_GetObjectItem(root, "a");
		cJSON* b = cJSON_GetObjectItem(root, "b");
		if (a && b && cJSON_IsNumber(a) && cJSON_IsNumber(b)) {
			int sa = a->valueint, sb = b->valueint;
			HostManager::instance().swapSlots(sa, sb);
			// 广播给所有客户端
			char* swapMsg = cJSON_PrintUnformatted(root);
			broadcastAll(swapMsg, strlen(swapMsg));
			cJSON_free(swapMsg);
		}
	}

	cJSON_Delete(root);
	return ESP_OK;
}
