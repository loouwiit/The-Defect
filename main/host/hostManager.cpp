/**
 * @file hostManager.cpp
 * @brief 跨主机连接管理器
 */

#include "hostManager.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "websocket/wsClient.hpp"
#include "host/wsConsoleHandler.hpp"
#include "wifi/mdns.hpp"
#include <esp_log.h>
#include <cstring>
#include <cJSON.h>

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

	m_consoleClient->setOnConnected([this]() {
		ESP_LOGI(TAG, "console channel connected");

		// 发送注册消息（hostname 用本机主机名，勿用远程主机名）
		const char* myHostname = mdnsGetConfiguredHostname();
		if (!myHostname || !myHostname[0]) myHostname = "esp32p4";
		char regMsg[128];
		int n = snprintf(regMsg, sizeof(regMsg),
			R"({"type":"register","hostname":"%s","gamepad_count":4})",
			myHostname);
		m_consoleClient->sendText(regMsg, n);
		});

	m_consoleClient->setOnDisconnected([this]() {
		ESP_LOGI(TAG, "console channel disconnected");

		VirtualGamepad::instance().stopForwarder();

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

	m_consoleClient->setOnMessage([this](const char* data, size_t len)
		{
			cJSON* root = cJSON_Parse(data);
			if (!root) { cJSON_Delete(root); return; }

			cJSON* typeItem = cJSON_GetObjectItem(root, "type");
			if (!typeItem || !cJSON_IsString(typeItem)) { cJSON_Delete(root); return; }
			const char* type = typeItem->valuestring;

			if (strcmp(type, "registered") == 0) {
				cJSON* slotItem = cJSON_GetObjectItem(root, "slot");
				if (slotItem && cJSON_IsNumber(slotItem)) {
					m_mySlot = slotItem->valueint;
					m_isClient = true;
					ESP_LOGI(TAG, "registered as slot %d", m_mySlot);

					// 启动 UDP 转发线程
					VirtualGamepad::instance().startForwarder();

					if (m_onRoleChange) m_onRoleChange(true);
				}
			}
			else if (strcmp(type, "slots_update") == 0) {
				cJSON* slotsArr = cJSON_GetObjectItem(root, "slots");
				if (!slotsArr || !cJSON_IsArray(slotsArr)) { cJSON_Delete(root); return; }

				uint8_t hostMask[4] = {};
				int idx = 0;
				cJSON* slotItem;
				cJSON_ArrayForEach(slotItem, slotsArr) {
					if (idx >= 4) break;
					cJSON* s = cJSON_GetObjectItem(slotItem, "slot");
					cJSON* hn = cJSON_GetObjectItem(slotItem, "hostname");
					cJSON* con = cJSON_GetObjectItem(slotItem, "connected");
					cJSON* gp = cJSON_GetObjectItem(slotItem, "gamepad_count");
					cJSON* mk = cJSON_GetObjectItem(slotItem, "mask");
					int slotId = s && cJSON_IsNumber(s) ? s->valueint : -1;
					if (slotId >= 0 && slotId < 4) {
						m_slots[slotId].connected = con && cJSON_IsBool(con) ? cJSON_IsTrue(con) : false;
						if (hn && cJSON_IsString(hn))
							strncpy(m_slots[slotId].hostname, hn->valuestring, sizeof(m_slots[slotId].hostname) - 1);
						if (gp && cJSON_IsNumber(gp))
							m_slots[slotId].gamepadCount = gp->valueint;
						if (mk && cJSON_IsNumber(mk))
							hostMask[slotId] = (uint8_t)mk->valueint;
					}
					idx++;
				}

				// 交换后重新定位自己的 slot：匹配本机主机名
				const char* myHostname = mdnsGetConfiguredHostname();
				if (!myHostname) myHostname = "";
				int myNewSlot = -1;
				for (int i = 0; i < 4; i++) {
					if (m_slots[i].connected && myHostname[0] &&
						strcmp(m_slots[i].hostname, myHostname) == 0) {
						myNewSlot = i;
						break;
					}
				}
				if (myNewSlot >= 0) {
					if (myNewSlot != m_mySlot)
						ESP_LOGI(TAG, "slot relocated: %d → %d", m_mySlot, myNewSlot);
					m_mySlot = myNewSlot;
				}

				// 应用主机下发的 mask：从机本地 mask = 反码
				uint8_t myMask[4] = {0, 0, 0, 0};
				if (m_mySlot >= 0 && m_mySlot < 4)
					myMask[0] = (~hostMask[m_mySlot]) & 0x0F;
				VirtualGamepad::instance().setAllMasks(myMask);
				for (int i = 0; i < 4; i++)
					m_hostMask[i] = myMask[i];

				if (m_onSlotUpdate) m_onSlotUpdate(m_slots);
			}

			cJSON_Delete(root);
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

void HostManager::setHostMask(const uint8_t mask[4])
{
	for (int i = 0; i < 4; i++)
		m_hostMask[i] = mask[i];
}

void HostManager::disconnectFromHost()
{
	// 第一步：通知主机我要优雅离开
	if (m_consoleClient)
	{
		const char* msg = R"({"type":"unregister"})";
		m_consoleClient->sendText(msg, strlen(msg));
		vTaskDelay(pdMS_TO_TICKS(50));  // 等消息发出去

		m_consoleClient->disconnect();
		delete m_consoleClient;
		m_consoleClient = nullptr;
	}

	// 第二步：清理各游戏 WS 连接池
	for (auto& e : m_wsPool) {
		if (e.client) {
			e.client->disconnect();
			delete e.client;
			e.client = nullptr;
		}
		e.uri[0] = '\0';
	}

	// 第三步：重置本机槽位（断开后本机是独立主机）
	{
		const char* myHostname = mdnsGetConfiguredHostname();
		m_slots[0] = {0, "", "", 0, true};
		if (myHostname)
			strncpy(m_slots[0].hostname, myHostname, sizeof(m_slots[0].hostname) - 1);
	}
	for (int i = 1; i < 4; i++)
		m_slots[i] = {i, "", "", 0, false};

	// 第四步：重置角色与连接信息
	m_isClient = false;
	m_mySlot = -1;
	m_remoteHostname[0] = '\0';
	m_remoteIp[0] = '\0';

	notifySlotUpdate();
	if (m_onRoleChange) m_onRoleChange(false);
	ESP_LOGI(TAG, "disconnected, restored host role");
}

void HostManager::disconnectClient(int slotId)
{
	if (slotId < 1 || slotId > 3)
	{
		ESP_LOGW(TAG, "invalid slot %d for disconnect", slotId);
		return;
	}
	wsConsoleDisconnectSlot(slotId);
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
	m_slots[slotId] = { slotId, "", "", 0, false };
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
	// swap 的同时交换 mask
	uint8_t maskTmp = m_hostMask[slotA];
	m_hostMask[slotA] = m_hostMask[slotB];
	m_hostMask[slotB] = maskTmp;
	ESP_LOGI(TAG, "swapped slot %d <-> %d (mask %02X <-> %02X)", slotA, slotB,
		m_hostMask[slotA], m_hostMask[slotB]);
	notifySlotUpdate();
}

void HostManager::notifySlotUpdate()
{
	if (m_onSlotUpdate) m_onSlotUpdate(m_slots);
}
