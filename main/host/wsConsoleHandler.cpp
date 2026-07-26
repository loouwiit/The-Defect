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
#include <lwip/sockets.h>

static constexpr char TAG[] = "WSConsole";

// ── fd → IP 缓存（握手时获取，后续直接使用）──
static constexpr int MAX_IP_CACHE = 16;
static struct {
	int fd;
	char ip[17];
} s_ipCache[MAX_IP_CACHE] = {};
static int s_ipCacheCount = 0;

/// 缓存 fd 对应的 IP（兼容 IPv4-mapped IPv6）
static void cachePeerIp(int fd)
{
	struct sockaddr_storage peer;
	socklen_t peerLen = sizeof(peer);
	if (getpeername(fd, (struct sockaddr*)&peer, &peerLen) != 0) {
		ESP_LOGW(TAG, "getpeername(fd=%d) failed: errno=%d", fd, errno);
		return;
	}
	for (int i = 0; i < s_ipCacheCount; i++) {
		if (s_ipCache[i].fd == fd) return;
	}
	if (s_ipCacheCount < MAX_IP_CACHE) {
		auto& c = s_ipCache[s_ipCacheCount++];
		c.fd = fd;

		if (peer.ss_family == AF_INET) {
			inet_ntop(AF_INET,
			          &((struct sockaddr_in*)&peer)->sin_addr,
			          c.ip, sizeof(c.ip));
		}
#if CONFIG_LWIP_IPV6
		else if (peer.ss_family == AF_INET6) {
			struct sockaddr_in6* s6 = (struct sockaddr_in6*)&peer;
			if (IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
				inet_ntop(AF_INET,
				          s6->sin6_addr.s6_addr + 12,
				          c.ip, sizeof(c.ip));
			} else {
				inet_ntop(AF_INET6,
				          &s6->sin6_addr,
				          c.ip, sizeof(c.ip));
			}
		}
#endif
		else {
			snprintf(c.ip, sizeof(c.ip), "unknown");
		}

		ESP_LOGI(TAG, "cached fd=%d -> %s", fd, c.ip);
	}
}

/// 从缓存获取 IP
static const char* getCachedIp(int fd)
{
	for (int i = 0; i < s_ipCacheCount; i++) {
		if (s_ipCache[i].fd == fd) return s_ipCache[i].ip;
	}
	return "";
}

/// 清理缓存的 fd
static void removeCachedIp(int fd)
{
	for (int i = 0; i < s_ipCacheCount; i++) {
		if (s_ipCache[i].fd == fd) {
			s_ipCache[i] = s_ipCache[--s_ipCacheCount];
			return;
		}
	}
}

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

// ── 全量 slots_update ──
static void broadcastSlotsUpdate(int excludeFd)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", "slots_update");

	cJSON* slotsArr = cJSON_CreateArray();
	const auto* slots = HostManager::instance().getSlots();
	for (int i = 0; i < 4; i++)
	{
		cJSON* slot = cJSON_CreateObject();
		cJSON_AddNumberToObject(slot, "slot", slots[i].slotId);
		cJSON_AddStringToObject(slot, "hostname", slots[i].hostname);
		cJSON_AddBoolToObject(slot, "connected", slots[i].connected);
		cJSON_AddNumberToObject(slot, "gamepad_count", slots[i].gamepadCount);
		cJSON_AddNumberToObject(slot, "mask", HostManager::instance().getHostMask()[i]);
		cJSON_AddItemToArray(slotsArr, slot);
	}
	cJSON_AddItemToObject(root, "slots", slotsArr);

	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (!str) return;

	broadcastExcept(str, strlen(str), excludeFd);
	cJSON_free(str);
}

// ════════════════════════════════════════════════════════════════
//  Handler
// ════════════════════════════════════════════════════════════════

esp_err_t wsConsoleHandler(httpd_req_t* req)
{
	/* ── 握手 ── */
	if (req->method == HTTP_GET) {
		int fd = httpd_req_to_sockfd(req);
		cachePeerIp(fd);
		ESP_LOGI(TAG, "WebSocket handshake /ws/console (fd=%d)", fd);
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
			InputHost::instance().unregisterClient(fd);
			HostManager::instance().onClientDisconnected(hostId);
			broadcastSlotsUpdate(fd);
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
		const char* peerIp = getCachedIp(fd);
		int existingHostId = findHostIdByFd(fd);
		if (existingHostId > 0) {
			// 已注册，更新信息
			HostManager::instance().onClientRegistered(
				existingHostId, hostname, peerIp, gamepadCount);
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
			hostId, hostname, getCachedIp(fd), gamepadCount);

		// 全量广播 slots_update
		broadcastSlotsUpdate(-1);

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
	else if (strcmp(type, "unregister") == 0) {
		int hostId = findHostIdByFd(fd);
		if (hostId > 0) {
			ESP_LOGI(TAG, "client hostId=%d unregistered gracefully", hostId);
			InputHost::instance().unregisterClient(fd);
			HostManager::instance().onClientDisconnected(hostId);
			broadcastSlotsUpdate(-1);
		}
		removeClient(fd);
	}
	else if (strcmp(type, "slot_swap") == 0) {
		cJSON* a = cJSON_GetObjectItem(root, "a");
		cJSON* b = cJSON_GetObjectItem(root, "b");
		if (a && b && cJSON_IsNumber(a) && cJSON_IsNumber(b)) {
			int sa = a->valueint, sb = b->valueint;
			wsConsoleHandleSlotSwap(sa, sb);
		}
	}

	cJSON_Delete(root);
	return ESP_OK;
}

void wsConsoleBroadcastSlotsUpdate()
{
	broadcastSlotsUpdate(-1);
}

void wsConsoleDisconnectSlot(int slotId)
{
	if (slotId < 1 || slotId > 3) return;

	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++)
	{
		if (s_clients[i].hostId == slotId && s_clients[i].fd > 0)
		{
			int fd = s_clients[i].fd;
			ESP_LOGI(TAG, "host disconnecting slot %d (fd=%d)", slotId, fd);

			// 先清理状态
			InputHost::instance().unregisterClient(fd);
			HostManager::instance().onClientDisconnected(slotId);
			broadcastSlotsUpdate(-1);
			removeClient(fd);

			// 关闭连接
			wsServerCloseClient(fd);
			return;
		}
	}
	ESP_LOGW(TAG, "slot %d: no client found to disconnect", slotId);
}

void wsConsoleHandleSlotSwap(int slotA, int slotB)
{
	if (slotA == slotB || slotA < 1 || slotA > 3 || slotB < 1 || slotB > 3)
	{
		ESP_LOGW(TAG, "invalid swap: %d <-> %d", slotA, slotB);
		return;
	}

	ESP_LOGI(TAG, "slot swap: %d <-> %d", slotA, slotB);

	// 1. 交换 HostManager 的槽位信息 + mask
	HostManager::instance().swapSlots(slotA, slotB);

	// 2. 交换 s_clients 的 hostId 追踪
	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++) {
		if (s_clients[i].hostId == slotA) s_clients[i].hostId = -1;
		else if (s_clients[i].hostId == slotB) s_clients[i].hostId = slotA;
	}
	for (int i = 0; i < MAX_CONSOLE_CLIENTS; i++) {
		if (s_clients[i].hostId == -1) s_clients[i].hostId = slotB;
	}

	// 3. 交换 InputHost 的 fd→hostId 映射
	InputHost::instance().swapHostIds(slotA, slotB);

	// 4. 全量广播
	broadcastSlotsUpdate(-1);
}
