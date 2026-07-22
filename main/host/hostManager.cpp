#include "hostManager.hpp"
#include "wifi/wsClient.hpp"
#include "wifi/mdns.hpp"
#include "virtualGamepad.hpp"
#include "bleGamepad/gamepadState.hpp"
#include "task/task.hpp"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>

static constexpr char TAG[] = "HostManager";

// ============================================================
//  单例
// ============================================================

HostManager& HostManager::instance()
{
    static HostManager s_inst;
    return s_inst;
}

HostManager::HostManager()
{
    // 初始化槽位
    for (int i = 0; i < MAX_CLIENTS; i++) {
        m_slots[i].slotId = i;
        m_slots[i].connected = false;
        m_slots[i].gamepadCount = 0;
        m_slots[i].hostname[0] = '\0';
        m_slots[i].ip[0] = '\0';
    }
    // Slot 0 = 自身
    m_slots[0].connected = true;
    m_slots[0].gamepadCount = 4;
    const char* selfName = mdnsGetConfiguredHostname();
    strncpy(m_slots[0].hostname, selfName, sizeof(m_slots[0].hostname) - 1);
    strncpy(m_selfHostname, selfName, sizeof(m_selfHostname) - 1);

    ESP_LOGI(TAG, "initialized (hostname=%s)", selfName);
}

// ============================================================
//  角色
// ============================================================

bool HostManager::isHost() const { return !m_isClient; }
bool HostManager::isClient() const { return m_isClient; }

// ============================================================
//  从机侧 — 连接远程主机
// ============================================================

bool HostManager::connectToHost(const char* ip, uint16_t port)
{
    if (m_isClient) {
        ESP_LOGW(TAG, "already connected to a host, disconnect first");
        return false;
    }

    ESP_LOGI(TAG, "connecting to host %s:%u ...", ip, port);

    // 保存远程主机信息
    strncpy(m_remoteIp, ip, sizeof(m_remoteIp) - 1);
    m_remotePort = port;

    // 创建 /ws/console 连接
    m_consoleClient = new WsClient();

    // 设置消息回调
    m_consoleClient->setOnMessage([this](const char* data, size_t len) {
        // 从 JSON 中提取字段
        auto findInt = [&](const char* key) -> int {
            const char* p = strstr(data, key);
            if (!p) return -1;
            p += strlen(key);
            while (*p == ' ' || *p == ':') p++;
            return atoi(p);
        };

        if (strstr(data, "\"type\":\"registered\"")) {
            int slot = findInt("\"slot\"");
            if (slot >= 0) {
                ESP_LOGI(TAG, "assigned slot %d", slot);
                m_myAssignedSlot = (int8_t)slot;
            }

            // 尝试解析全量槽位（新协议）
            parseSlotSync(data);

            if (m_onSlotUpdate) m_onSlotUpdate(0);
        }
        else if (strstr(data, "\"type\":\"slot_sync\"")) {
            parseSlotSync(data);
            if (m_onSlotUpdate) m_onSlotUpdate(-1);
        }
        else if (strstr(data, "\"type\":\"mask_sync\"")) {
            // 解析掩码并更新本地 VirtualGamepad
            cJSON* root = cJSON_Parse(data);
            if (root) {
                cJSON* maskArr = cJSON_GetObjectItem(root, "mask");
                if (maskArr && cJSON_IsArray(maskArr) && cJSON_GetArraySize(maskArr) >= 4) {
                    uint16_t mask[4];
                    for (int i = 0; i < 4; i++) {
                        cJSON* item = cJSON_GetArrayItem(maskArr, i);
                        mask[i] = (item && cJSON_IsNumber(item)) ? (uint16_t)item->valueint : 0;
                    }
                    VirtualGamepad::instance().setInputMask(mask);
                    ESP_LOGI(TAG, "mask_sync: %u %u %u %u", mask[0], mask[1], mask[2], mask[3]);
                }
                cJSON_Delete(root);
            }
        }
        });

    m_consoleClient->setOnConnected([this]() {
        ESP_LOGI(TAG, "console channel connected, registering...");
        // 发送注册消息
        char regMsg[128];
        int n = snprintf(regMsg, sizeof(regMsg),
            "{\"type\":\"register\",\"hostname\":\"%s\",\"gamepad_count\":%d}",
            m_selfHostname, 4);  // 本机最多 4 手柄
        if (n > 0) {
            m_consoleClient->sendText(regMsg, n);
        }
        });

    m_consoleClient->setOnDisconnected([this]() {
        ESP_LOGW(TAG, "console channel disconnected");
        });

    if (!m_consoleClient->connect(ip, port, "/ws/console")) {
        ESP_LOGE(TAG, "console channel connect failed");
        delete m_consoleClient;
        m_consoleClient = nullptr;
        m_remoteIp[0] = '\0';
        return false;
    }

    // 创建输入转发队列
    m_inputFwdQueue = xQueueCreate(1, sizeof(GamepadInputEvent));

    // 启动控制通道轮询 Task（处理收 + 发，避免多 Task 竞争同一 WsClient）
    m_consoleUpdateTask = (void*)::Task::addTask([](void* param) -> TickType_t {
        auto* hm = static_cast<HostManager*>(param);
        if (!hm->m_consoleClient) return 50;

        // 处理接收消息
        hm->m_consoleClient->update();

        // 发送待转发的输入
        GamepadInputEvent evt;
        if (hm->m_inputFwdQueue &&
            xQueueReceive(hm->m_inputFwdQueue, &evt, 0) == pdTRUE)
        {
            int8_t slot = hm->myAssignedSlot();
            ESP_LOGI(TAG, "fwd P%d btn=0x%04x", evt.playerId, evt.state.buttons);
            if (slot >= 0) {
                char j[200]; int n = snprintf(j, sizeof(j),
                    "{\"type\":\"input\",\"hostId\":%d,\"localPlayer\":%d,"
                    "\"buttons\":%u,\"lx\":%u,\"ly\":%u,"
                    "\"rx\":%u,\"ry\":%u,\"lt\":%u,\"rt\":%u,\"dpad\":%u}",
                    slot, evt.playerId, evt.state.buttons,
                    evt.state.lx, evt.state.ly,
                    evt.state.rx, evt.state.ry,
                    evt.state.lt, evt.state.rt,
                    evt.state.dpad);
                if (n > 0) {
                    bool ok = hm->sendOnConsole(j, n);
                    ESP_LOGI(TAG, "sendOnConsole %s", ok ? "OK" : "FAIL");
                }
            }
        }

        return 50;
    }, "consolePoll", this, 50, ::Task::Affinity::None);

    m_isClient = true;

    // 更新自身槽位显示（在 host 视角我们是某个 slot）
    // 实际 slot 编号等待 registered 回复
    m_slots[0].connected = true;

    ESP_LOGI(TAG, "connected to host %s:%u", ip, port);
    return true;
}

void HostManager::disconnectFromHost()
{
    if (!m_isClient) return;

    ESP_LOGI(TAG, "disconnecting from host");

    // 断开所有 WsClient
    for (auto& entry : m_wsPool) {
        if (entry.client) {
            entry.client->disconnect();
            delete entry.client;
            entry.client = nullptr;
            entry.uri[0] = '\0';
        }
    }

    // 停止控制通道轮询
    if (m_consoleUpdateTask) {
        ::Task::removeTask(static_cast<::Task*>(m_consoleUpdateTask));
        m_consoleUpdateTask = nullptr;
    }

    if (m_consoleClient) {
        m_consoleClient->disconnect();
        delete m_consoleClient;
        m_consoleClient = nullptr;
    }

    m_isClient = false;
    m_myAssignedSlot = -1;
    m_remoteIp[0] = '\0';
    m_remoteHostname[0] = '\0';

    // 恢复本机 Slot 0
    m_slots[0].slotId = 0;
    m_slots[0].connected = true;
    const char* name = mdnsGetConfiguredHostname();
    strncpy(m_slots[0].hostname, name, sizeof(m_slots[0].hostname) - 1);

    if (m_onSlotUpdate) m_onSlotUpdate(0);

    ESP_LOGI(TAG, "disconnected, restored to host mode");
}

bool HostManager::isConnectedToHost() const
{
    return m_isClient && m_consoleClient && m_consoleClient->isConnected();
}

const char* HostManager::getRemoteHostname() const
{
    return m_remoteHostname;
}

// ============================================================
//  从机侧 — 获取 WsClient（连接池）
// ============================================================

WsClient* HostManager::getWsConnection(const char* uri)
{
    if (!m_isClient) {
        ESP_LOGW(TAG, "not in client mode, cannot get connection");
        return nullptr;
    }

    // 冷却时间数组（函数级 static，新建和重连共用）
    static TickType_t s_lastConnect[MAX_WS_URIS]{};

    // 查找已有连接
    for (auto& entry : m_wsPool) {
        if (entry.client && strcmp(entry.uri, uri) == 0) {
            int idx = &entry - m_wsPool;
            TickType_t now = xTaskGetTickCount();

            // 已连接
            if (entry.client->isConnected()) return entry.client;

            // 未连接且冷却中（异步连接进行中或刚断开）
            if (now - s_lastConnect[idx] < pdMS_TO_TICKS(3000)) {
                return entry.client;
            }

            // 冷却已过，执行重连
            s_lastConnect[idx] = now;
            ESP_LOGW(TAG, "%s reconnecting...", uri);
            entry.client->disconnect();
            delete entry.client;
            entry.client = new WsClient();
            if (!entry.client->connect(m_remoteIp, m_remotePort, uri)) {
                ESP_LOGE(TAG, "reconnect %s failed", uri);
                delete entry.client;
                entry.client = nullptr;
                entry.uri[0] = '\0';
                return nullptr;
            }
            return entry.client;
        }
    }

    // 创建新连接
    WsClient* client = new WsClient();
    if (!client->connect(m_remoteIp, m_remotePort, uri)) {
        ESP_LOGE(TAG, "connect %s failed", uri);
        delete client;
        return nullptr;
    }

    // 存入连接池 + 启动冷却
    for (auto& entry : m_wsPool) {
        if (!entry.client) {
            strncpy(entry.uri, uri, sizeof(entry.uri) - 1);
            entry.client = client;
            int idx = &entry - m_wsPool;
            s_lastConnect[idx] = xTaskGetTickCount();
            break;
        }
    }

    return client;
}

// ============================================================
//  主机侧 — 槽位管理
// ============================================================

int HostManager::getConnectedClientCount() const
{
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (m_slots[i].connected) count++;
    }
    return count;
}

const HostManager::ClientSlot* HostManager::getSlots() const
{
    return m_slots;
}

bool HostManager::swapSlots(int slotA, int slotB)
{
    if (slotA < 0 || slotA >= MAX_CLIENTS) return false;
    if (slotB < 0 || slotB >= MAX_CLIENTS) return false;
    if (slotA == slotB) return false;
    if (slotA == 0 || slotB == 0) {
        ESP_LOGW(TAG, "cannot swap slot 0 (self)");
        return false;
    }

    ESP_LOGI(TAG, "swap slot %d <-> %d", slotA, slotB);

    // 交换槽位内容
    ClientSlot tmp = m_slots[slotA];
    m_slots[slotA] = m_slots[slotB];
    m_slots[slotB] = tmp;
    m_slots[slotA].slotId = slotA;
    m_slots[slotB].slotId = slotB;

    // 更新 fdMap 中的 hostId
    for (auto& entry : m_fdMap) {
        if (entry.hostId == slotA) entry.hostId = slotB;
        else if (entry.hostId == slotB) entry.hostId = slotA;
    }

    if (m_onSlotUpdate) m_onSlotUpdate(-1);

    // 广播交换事件给所有从机
    if (m_broadcastFn) {
        char msg[128];
        int n = snprintf(msg, sizeof(msg),
            "{\"type\":\"slot_swap\",\"a\":%d,\"b\":%d}", slotA, slotB);
        if (n > 0) m_broadcastFn(msg, n);
    }

    return true;
}

// ============================================================
//  主机侧 — 注册/注销从机
// ============================================================

int HostManager::registerClient(int fd, const char* hostname, int gamepadCount)
{
    // 查找空槽位
    int slot = findEmptySlot();
    if (slot < 0) {
        ESP_LOGW(TAG, "no empty slot for client %s", hostname);
        return -1;
    }

    ESP_LOGI(TAG, "register client %s -> slot %d (fd=%d, gamepads=%d)",
             hostname, slot, fd, gamepadCount);

    // 填充槽位
    m_slots[slot].connected = true;
    m_slots[slot].gamepadCount = gamepadCount;
    strncpy(m_slots[slot].hostname, hostname, sizeof(m_slots[slot].hostname) - 1);
    // IP from fd is not directly available here, set from existing info or leave empty
    m_slots[slot].ip[0] = '\0';

    // 记录 fd 映射
    for (auto& entry : m_fdMap) {
        if (entry.fd < 0) {
            entry.fd = fd;
            entry.hostId = slot;
            break;
        }
    }

    if (m_onSlotUpdate) m_onSlotUpdate(slot);
    return slot;
}

void HostManager::unregisterClient(int fd)
{
    int slot = findSlotByFd(fd);
    if (slot < 0) return;

    ESP_LOGI(TAG, "unregister client fd=%d -> slot %d", fd, slot);

    m_slots[slot].connected = false;
    m_slots[slot].gamepadCount = 0;
    m_slots[slot].hostname[0] = '\0';
    m_slots[slot].ip[0] = '\0';

    for (auto& entry : m_fdMap) {
        if (entry.fd == fd) {
            entry.fd = -1;
            entry.hostId = -1;
            break;
        }
    }

    if (m_onSlotUpdate) m_onSlotUpdate(slot);
}

int HostManager::getHostIdByFd(int fd) const
{
    for (auto& entry : m_fdMap) {
        if (entry.fd == fd) return entry.hostId;
    }
    return -1;
}

// ============================================================
//  事件
// ============================================================

QueueHandle_t HostManager::getInputQueue()
{
    return m_inputFwdQueue;
}

bool HostManager::sendOnConsole(const char* data, size_t len)
{
    if (!m_consoleClient || !m_consoleClient->isConnected()) return false;
    return m_consoleClient->sendText(data, len);
}

void HostManager::setOnSlotUpdate(std::function<void(int slotId)> cb)
{
    m_onSlotUpdate = std::move(cb);
}

void HostManager::setBroadcastFn(std::function<void(const char*, size_t)> fn)
{
    m_broadcastFn = std::move(fn);
}

// ============================================================
//  全量槽位同步解析
// ============================================================

void HostManager::parseSlotSync(const char* json)
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    cJSON* slotsArr = cJSON_GetObjectItem(root, "slots");
    if (!slotsArr || !cJSON_IsArray(slotsArr)) {
        cJSON_Delete(root);
        return;
    }

    int count = cJSON_GetArraySize(slotsArr);
    for (int i = 0; i < count && i < MAX_CLIENTS; i++) {
        cJSON* item = cJSON_GetArrayItem(slotsArr, i);
        if (!item) continue;

        cJSON* connected = cJSON_GetObjectItem(item, "connected");
        cJSON* hostname = cJSON_GetObjectItem(item, "hostname");
        cJSON* gp = cJSON_GetObjectItem(item, "gamepad_count");

        m_slots[i].connected = (connected && cJSON_IsBool(connected)) ? cJSON_IsTrue(connected) : false;
        m_slots[i].gamepadCount = (gp && cJSON_IsNumber(gp)) ? gp->valueint : 0;
        m_slots[i].slotId = i;

        if (m_slots[i].connected && hostname && cJSON_IsString(hostname)) {
            strncpy(m_slots[i].hostname, hostname->valuestring, sizeof(m_slots[i].hostname) - 1);
        } else {
            m_slots[i].hostname[0] = '\0';
        }
    }

    cJSON_Delete(root);
}

// ============================================================
//  内部方法
// ============================================================

int HostManager::findEmptySlot() const
{
    for (int i = 1; i < MAX_CLIENTS; i++) {  // Slot 0 是自身
        if (!m_slots[i].connected) return i;
    }
    return -1;
}

int HostManager::findSlotByFd(int fd) const
{
    for (auto& entry : m_fdMap) {
        if (entry.fd == fd) return entry.hostId;
    }
    return -1;
}
