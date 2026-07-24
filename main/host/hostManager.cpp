/**
 * @file hostManager.cpp
 * @brief 跨主机连接管理器
 */

#include "hostManager.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "websocket/wsClient.hpp"
#include <esp_log.h>
#include <cstring>

static constexpr char TAG[] = "HostManager";

// ════════════════════════════════════════════════════════════════
//  单例
// ════════════════════════════════════════════════════════════════

HostManager& HostManager::instance()
{
	static HostManager s_instance{};
	return s_instance;
}

// ════════════════════════════════════════════════════════════════
//  从机侧：连接 / 断开
// ════════════════════════════════════════════════════════════════

bool HostManager::connectToHost(const char* ip, uint16_t port, const char* hostname)
{
	if (m_consoleClient)
	{
		ESP_LOGW(TAG, "already connected to %s, disconnect first", m_remoteHostname);
		return false;
	}

	strncpy(m_remoteIp, ip, sizeof(m_remoteIp) - 1);
	m_remotePort = port;

	// 创建 /ws/console 连接
	m_consoleClient = new WsClient();

	m_consoleClient->setOnConnected([this, hostname]() {
		ESP_LOGI(TAG, "console channel connected");

		// 发送注册消息
		char regMsg[128];
		int n = snprintf(regMsg, sizeof(regMsg),
			R"({"type":"register","hostname":"%s","gamepad_count":4})",
			hostname && hostname[0] ? hostname : "esp32p4");
		m_consoleClient->sendText(regMsg, n);
	});

	m_consoleClient->setOnDisconnected([this]() {
		ESP_LOGI(TAG, "console channel disconnected");
		// 清理连接池
		for (auto& e : m_wsPool) {
			if (e.client) { delete e.client; e.client = nullptr; }
			e.uri[0] = '\0';
		}
		m_consoleClient = nullptr;
		m_isClient = false;
		m_mySlot = -1;
		if (m_onRoleChange) m_onRoleChange(false);
	});

	m_consoleClient->setOnMessage([this](const char* data, size_t len) {
		// 简单 JSON 解析 — 只关心 type 字段
		// 生产环境建议用 cJSON，这里用 strstr 简化
		if (strstr(data, "\"type\":\"registered\"")) {
			// {"type":"registered","slot":1}
			const char* slotStr = strstr(data, "\"slot\":");
			if (slotStr) {
				m_mySlot = atoi(slotStr + 7);
				m_isClient = true;
				ESP_LOGI(TAG, "registered as slot %d", m_mySlot);
				if (m_onRoleChange) m_onRoleChange(true);
			}
		}
		else if (strstr(data, "\"type\":\"client_joined\"")) {
			// {"type":"client_joined","slot":1,"hostname":"esp32p4-a","gamepad_count":2}
			int slot = -1, gp = 0;
			char name[64] = {};
			const char* s = strstr(data, "\"slot\":");
			if (s) slot = atoi(s + 7);
			const char* n = strstr(data, "\"hostname\":\"");
			if (n) { n += 11; int i = 0; while (*n && *n != '"' && i < 63) name[i++] = *n++; name[i] = 0; }
			const char* g = strstr(data, "\"gamepad_count\":");
			if (g) gp = atoi(g + 16);
			if (slot >= 1 && slot <= 3)
				onClientRegistered(slot, name, "", gp);
		}
		else if (strstr(data, "\"type\":\"client_left\"")) {
			int slot = -1;
			const char* s = strstr(data, "\"slot\":");
			if (s) slot = atoi(s + 7);
			if (slot >= 1 && slot <= 3)
				onClientDisconnected(slot);
		}
		else if (strstr(data, "\"type\":\"slot_swap\"")) {
			int a = -1, b = -1;
			const char* sa = strstr(data, "\"a\":");
			if (sa) a = atoi(sa + 4);
			const char* sb = strstr(data, "\"b\":");
			if (sb) b = atoi(sb + 4);
			if (a >= 0 && b >= 0 && a < 4 && b < 4)
				swapSlots(a, b);
		}
	});

	bool ok = m_consoleClient->connect(ip, port, "/ws/console");
	if (!ok)
	{
		ESP_LOGE(TAG, "connect to %s:%u/ws/console failed", ip, port);
		delete m_consoleClient;
		m_consoleClient = nullptr;
		return false;
	}

	// 保存远程主机名（从机时的显示）
	if (hostname && hostname[0])
		strncpy(m_remoteHostname, hostname, sizeof(m_remoteHostname) - 1);
	else
		snprintf(m_remoteHostname, sizeof(m_remoteHostname), "%s", ip);

	ESP_LOGI(TAG, "connecting to %s:%u ...", ip, port);
	return true;
}

void HostManager::disconnectFromHost()
{
	if (m_consoleClient)
	{
		m_consoleClient->disconnect();
		delete m_consoleClient;
		m_consoleClient = nullptr;
	}
	for (auto& e : m_wsPool) {
		if (e.client) { delete e.client; e.client = nullptr; }
		e.uri[0] = '\0';
	}
	m_isClient = false;
	m_mySlot = -1;
	m_remoteHostname[0] = '\0';
	m_remoteIp[0] = '\0';
	notifySlotUpdate();
	if (m_onRoleChange) m_onRoleChange(false);
	ESP_LOGI(TAG, "disconnected, restored host role");
}

// ════════════════════════════════════════════════════════════════
//  连接池：getWsConnection
// ════════════════════════════════════════════════════════════════

WsClient* HostManager::createOrGetWsClient(const char* uri)
{
	if (!m_consoleClient) return nullptr;

	// 查找缓存
	for (auto& e : m_wsPool) {
		if (e.client && strcmp(e.uri, uri) == 0)
			return e.client;
	}

	// 找空位创建
	for (auto& e : m_wsPool) {
		if (!e.client) {
			e.client = new WsClient();
			strncpy(e.uri, uri, sizeof(e.uri) - 1);
			if (!e.client->connect(m_remoteIp, m_remotePort, uri)) {
				ESP_LOGE(TAG, "connect %s:%u%s failed", m_remoteIp, m_remotePort, uri);
				delete e.client;
				e.client = nullptr;
				e.uri[0] = '\0';
				return nullptr;
			}
			ESP_LOGI(TAG, "connected to %s", uri);
			return e.client;
		}
	}

	ESP_LOGW(TAG, "ws pool full, can't create %s", uri);
	return nullptr;
}

WsClient* HostManager::getWsConnection(const char* uri)
{
	return createOrGetWsClient(uri);
}

// ════════════════════════════════════════════════════════════════
//  槽位管理（主机侧）
// ════════════════════════════════════════════════════════════════

void HostManager::onClientRegistered(int slotId, const char* hostname,
                                      const char* ip, int gamepadCount)
{
	if (slotId < 0 || slotId >= 4) return;
	auto& s = m_slots[slotId];
	s.slotId = slotId;
	s.connected = true;
	s.gamepadCount = gamepadCount;
	if (hostname) strncpy(s.hostname, hostname, sizeof(s.hostname) - 1);
	if (ip)       strncpy(s.ip, ip, sizeof(s.ip) - 1);
	ESP_LOGI(TAG, "slot %d: %s (%d gamepads)", slotId, hostname, gamepadCount);
	notifySlotUpdate();
}

void HostManager::onClientDisconnected(int slotId)
{
	if (slotId < 0 || slotId >= 4) return;
	m_slots[slotId] = {slotId, "", "", 0, false};
	ESP_LOGI(TAG, "slot %d disconnected", slotId);
	notifySlotUpdate();
}

void HostManager::swapSlots(int slotA, int slotB)
{
	if (slotA < 0 || slotA >= 4 || slotB < 0 || slotB >= 4 || slotA == slotB)
		return;
	ClientSlot tmp = m_slots[slotA];
	m_slots[slotA] = m_slots[slotB];
	m_slots[slotB] = tmp;
	m_slots[slotA].slotId = slotA;
	m_slots[slotB].slotId = slotB;

	// 同时交换 VirtualGamepad 的掩码
	auto& vg = VirtualGamepad::instance();
	uint8_t maskA = vg.getHostMask(slotA);
	uint8_t maskB = vg.getHostMask(slotB);
	vg.setHostMask(slotA, maskB);
	vg.setHostMask(slotB, maskA);

	ESP_LOGI(TAG, "swapped slot %d <-> %d (masks: 0x%02X <-> 0x%02X)",
			 slotA, slotB, maskA, maskB);
	notifySlotUpdate();
}

void HostManager::notifySlotUpdate()
{
	if (m_onSlotUpdate) m_onSlotUpdate(m_slots);
}
