/**
 * @file hostManager.cpp
 * @brief 跨主机连接管理器
 */

#include "hostManager.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "websocket/wsClient.hpp"
#include "host/wsConsoleHandler.hpp"
#include "task/task.hpp"
#include "wifi/mdns.hpp"
#include <esp_log.h>
#include <cstring>
#include <vector>
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
	// 旧连接已断开但未清理 → 先同步清理（本函数通常在 UI/daemon 上下文，非 WS 回调，安全）
	bool needCleanup = false;
	{
		Lock lock{ m_mutex };
		needCleanup = (m_consoleClient != nullptr) && m_consoleDisconnected;
		if (m_consoleClient && !m_consoleDisconnected)
		{
			ESP_LOGW(TAG, "already connected to %s, disconnect first", m_remoteHostname);
			return false;
		}
	}
	if (needCleanup)
		cleanupConnection();

	{
		Lock lock{ m_mutex };
		strncpy(m_remoteIp, ip, sizeof(m_remoteIp) - 1);
		m_remotePort = port;
	}

	// 创建 /ws/console 连接
	WsClient* client = new WsClient();
	{
		Lock lock{ m_mutex };
		m_consoleClient = client;
		m_consoleDisconnected = false;
	}

	client->setOnConnected([this]() {
		ESP_LOGI(TAG, "console channel connected");
		{
			Lock lock{ m_mutex };
			m_consoleDisconnected = false;
		}

		// 发送注册消息（hostname 用本机主机名，勿用远程主机名）
		const char* myHostname = mdnsGetConfiguredHostname();
		if (!myHostname || !myHostname[0]) myHostname = "esp32p4";
		char regMsg[128];
		int n = snprintf(regMsg, sizeof(regMsg),
			R"({"type":"register","hostname":"%s","gamepad_count":4})",
			myHostname);
		// 锁内取快照防 UAF：清理路径 delete 前必先置空 m_consoleClient
		WsClient* aliveClient = nullptr;
		{
			Lock lock{ m_mutex };
			if (m_consoleClient && !m_consoleDisconnected)
				aliveClient = m_consoleClient;
		}
		if (aliveClient) aliveClient->sendText(regMsg, n);
		});

	// 断开回调不在 WS task 上下文直接 delete（esp_websocket_client_stop 会等待 task 退出 → 死锁）
	client->setOnDisconnected([this]() {
		ESP_LOGI(TAG, "console channel disconnected");

		VirtualGamepad::instance().stopForwarder();
		{
			Lock lock{ m_mutex };
			m_consoleDisconnected = true;
		}
		scheduleConnectionCleanup();
		notifyRoleChange(false);
		});

	client->setOnMessage([this](const char* data, size_t len)
		{
			cJSON* root = cJSON_Parse(data);
			if (!root) { cJSON_Delete(root); return; }

			cJSON* typeItem = cJSON_GetObjectItem(root, "type");
			if (!typeItem || !cJSON_IsString(typeItem)) { cJSON_Delete(root); return; }
			const char* type = typeItem->valuestring;

			if (strcmp(type, "registered") == 0) {
				cJSON* slotItem = cJSON_GetObjectItem(root, "slot");
				if (slotItem && cJSON_IsNumber(slotItem)) {
					{
						Lock lock{ m_mutex };
						m_mySlot = slotItem->valueint;
						m_isClient = true;
					}
					ESP_LOGI(TAG, "registered as slot %d", m_mySlot);

					// 启动 UDP 转发线程
					VirtualGamepad::instance().startForwarder();

					notifyRoleChange(true);
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
						{
							Lock lock{ m_mutex };
							m_slots[slotId].connected = con && cJSON_IsBool(con) ? cJSON_IsTrue(con) : false;
							if (hn && cJSON_IsString(hn))
								strncpy(m_slots[slotId].hostname, hn->valuestring, sizeof(m_slots[slotId].hostname) - 1);
							if (gp && cJSON_IsNumber(gp))
								m_slots[slotId].gamepadCount = gp->valueint;
							if (mk && cJSON_IsNumber(mk))
								hostMask[slotId] = (uint8_t)mk->valueint;
						}
					}
					idx++;
				}

				// 交换后重新定位自己的 slot：匹配本机主机名
				const char* myHostname = mdnsGetConfiguredHostname();
				if (!myHostname) myHostname = "";
				int myNewSlot = -1;
				{
					Lock lock{ m_mutex };
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
				}

				notifySlotUpdate();
			}

			cJSON_Delete(root);
		});

	bool ok = client->connect(ip, port, "/ws/console");
	if (!ok)
	{
		ESP_LOGE(TAG, "connect to %s:%u/ws/console failed", ip, port);
		cleanupConnection();
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
	Lock lock{ m_mutex };
	for (int i = 0; i < 4; i++)
		m_hostMask[i] = mask[i];
}

// ════════════════════════════════════════════════════════════════
//  统一清理
// ════════════════════════════════════════════════════════════════

void HostManager::scheduleConnectionCleanup()
{
	// 防重复投递：已有未清理连接才投递
	{
		Lock lock{ m_mutex };
		if (!m_consoleClient && !m_consoleDisconnected) return;
		if (m_wsPool[0].client == nullptr && m_consoleClient == nullptr) return;
	}
	Task::addTask([](void* p) -> TickType_t {
		static_cast<HostManager*>(p)->cleanupConnection();
		return Task::infinityTime;
	}, "hostmgr cleanup", this, 0, Task::Affinity::None);
}

void HostManager::cleanupConnection()
{
	// 先断开并销毁所有客户端
	// m_wsPool 的删除必须"锁内摘除 + 锁外 delete"（防止与 getWsConnection 并发 UAF）
	std::vector<WsClient*> poolToDelete;
	{
		Lock lock{ m_mutex };
		for (auto& e : m_wsPool) {
			if (e.client) { poolToDelete.push_back(e.client); e.client = nullptr; }
			e.uri[0] = '\0';
		}
	}
	for (auto* c : poolToDelete)
		delete c;

	// m_consoleClient 的删除也须锁内摘除 + 锁外 delete（~WsClient → disconnect → stop 死锁）
	WsClient* consoleToDelete = nullptr;
	m_mutex.lock();
	if (m_consoleClient)
	{
		consoleToDelete = m_consoleClient;
		m_consoleClient = nullptr;
	}
	m_consoleDisconnected = false;

	// 重置角色与连接信息
	m_isClient = false;
	m_mySlot = -1;
	m_remoteHostname[0] = '\0';
	m_remoteIp[0] = '\0';

	// 重置本机槽位（断开后本机是独立主机）
	{
		const char* myHostname = mdnsGetConfiguredHostname();
		m_slots[0] = {0, "", "", 0, true};
		if (myHostname)
			strncpy(m_slots[0].hostname, myHostname, sizeof(m_slots[0].hostname) - 1);
	}
	for (int i = 1; i < 4; i++)
		m_slots[i] = {i, "", "", 0, false};
	m_mutex.unlock();

	if (consoleToDelete)
		delete consoleToDelete;   // 解锁后销毁（stop 等 WS task 退出，可能触发回调持锁）

	// 解锁后通知（回调可能反向调用本管理器）
	notifySlotUpdate();
	notifyRoleChange(false);
	ESP_LOGI(TAG, "disconnected, restored host role");
}

void HostManager::disconnectFromHost()
{
	// 第一步：通知主机我要优雅离开（锁外 send——cleanup 持锁 delete 会等 WS task 退出，
	// 若在锁内 sendText 可能阻塞到停止，这里不持锁）
	if (m_consoleClient && m_consoleClient->isConnected())
	{
		const char* msg = R"({"type":"unregister"})";
		m_consoleClient->sendText(msg, strlen(msg));
		vTaskDelay(pdMS_TO_TICKS(50));  // 等消息发出去
	}

	// 第二步：投递统一清理（不在调用者上下文 delete——esp_websocket_client_stop 会等 task 退出）
	scheduleConnectionCleanup();
	ESP_LOGI(TAG, "disconnectFromHost: cleanup scheduled");
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
	// 防：console 通道已断开/未连接时白建游戏通道（返回已死连接）
	{
		Lock lock{ m_mutex };
		if (!m_consoleClient || m_consoleDisconnected) return nullptr;
	}

	// 查找缓存（含"已断开"检测：断开的死连接应重建）
	// 已断开 client 的删除必须"锁内摘除 + 锁外 delete"
	WsClient* deadClient = nullptr;
	{
		Lock lock{ m_mutex };
		for (auto& e : m_wsPool) {
			if (e.client && strcmp(e.uri, uri) == 0) {
				if (e.client->isConnected())
					return e.client;
				// 已断开 → 摘除，走重建（锁外 delete）
				deadClient = e.client;
				e.client = nullptr;
				e.uri[0] = '\0';
				break;
			}
		}
	}
	if (deadClient) delete deadClient;

	// 找空位创建
	{
		Lock lock{ m_mutex };
		for (auto& e : m_wsPool) {
			if (!e.client) {
				e.client = new WsClient();
				strncpy(e.uri, uri, sizeof(e.uri) - 1);
				if (!e.client->connect(m_remoteIp, m_remotePort, uri)) {
					ESP_LOGE(TAG, "connect %s:%u%s failed", m_remoteIp, m_remotePort, uri);
					WsClient* failed = e.client;
					e.client = nullptr;
					e.uri[0] = '\0';
					delete failed;
					return nullptr;
				}
				ESP_LOGI(TAG, "connected to %s", uri);
				return e.client;
			}
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
	{
		Lock lock{ m_mutex };
		if (slotId < 0 || slotId >= 4) return;
		auto& s = m_slots[slotId];
		s.slotId = slotId;
		s.connected = true;
		s.gamepadCount = gamepadCount;
		if (hostname) strncpy(s.hostname, hostname, sizeof(s.hostname) - 1);
		if (ip)       strncpy(s.ip, ip, sizeof(s.ip) - 1);
		ESP_LOGI(TAG, "slot %d: %s (%d gamepads)", slotId, hostname, gamepadCount);
	}
	notifySlotUpdate();
}

void HostManager::onClientDisconnected(int slotId)
{
	{
		Lock lock{ m_mutex };
		if (slotId < 0 || slotId >= 4) return;
		m_slots[slotId] = { slotId, "", "", 0, false };
		ESP_LOGI(TAG, "slot %d disconnected", slotId);
	}
	notifySlotUpdate();
}

void HostManager::swapSlots(int slotA, int slotB)
{
	{
		Lock lock{ m_mutex };
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
	}
	notifySlotUpdate();
}

void HostManager::notifySlotUpdate()
{
	// 锁内拷贝 slots + 回调（std::function 并发读写竞态），解锁后调用
	ClientSlot copy[4];
	std::function<void(const ClientSlot*)> cb;
	{
		Lock lock{ m_mutex };
		memcpy(copy, m_slots, sizeof(copy));
		cb = m_onSlotUpdate;
	}
	if (cb) cb(copy);
}

void HostManager::notifyRoleChange(bool isClient)
{
	std::function<void(bool)> cb;
	{
		Lock lock{ m_mutex };
		cb = m_onRoleChange;
	}
	if (cb) cb(isClient);
}
