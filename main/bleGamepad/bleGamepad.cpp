#include "bleGamepad.hpp"

#include "esp_log.h"
#include "esp_err.h"

// ESP HOSTED
#include "esp_hosted.h"

// NimBLE
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"

// HID Host
#include "esp_hidh.h"

// ble_store_config_init 在 nimble/ble_store.h 中声明
// 但该头文件不在标准包含路径中，这里前置声明
#ifdef __cplusplus
extern "C" {
#endif
void ble_store_config_init(void);
#ifdef __cplusplus
}
#endif

static constexpr char TAG[] = "BleGamepad";
static constexpr int INPUT_QUEUE_LEN = 16;

BleGamepad* BleGamepad::s_instance = nullptr;

// ── Singleton ──
BleGamepad& BleGamepad::instance()
{
    static BleGamepad inst;
    return inst;
}

// ── 回调转发 ──

// NimBLE GAP 事件（用于扫描发现）—— friend 声明在 hpp 中，此处不能 static
int bleGapEventCb(struct ble_gap_event* event, void* arg);

// NimBLE 同步/重置回调
static void (*s_prev_sync_cb)(void) = nullptr;
static void (*s_prev_reset_cb)(int reason) = nullptr;

void mySyncCb()
{
    ESP_LOGI(TAG, "NimBLE synced");
    auto& self = BleGamepad::instance();
    // 直接开始扫描——NimBLE 已就绪
    self.startScan();
    if (s_prev_sync_cb) s_prev_sync_cb();
}

void myResetCb(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
    if (s_prev_reset_cb) s_prev_reset_cb(reason);
}

// HID Host 事件回调
void BleGamepad::hidhCallback(void* /*handler_args*/, esp_event_base_t /*base*/, int32_t id, void* event_data)
{
    auto& self = instance();
    auto event = static_cast<esp_hidh_event_t>(id);
    auto* param = static_cast<esp_hidh_event_data_t*>(event_data);

    switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
        if (param->open.status != ESP_OK) {
            ESP_LOGE(TAG, "OPEN failed");
            break;
        }
        auto* dev = param->open.dev;
        const uint8_t* bda = esp_hidh_dev_bda_get(dev);
        const char* name = esp_hidh_dev_name_get(dev);
        if (bda) {
            ESP_LOGI(TAG, "OPEN: %s [%02x:%02x:%02x:%02x:%02x:%02x]",
                     name ? name : "?",
                     bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
        } else {
            ESP_LOGI(TAG, "OPEN: %s [??:??:??:??:??:??]", name ? name : "?");
        }

        // 分配 playerId
        int pid = self.allocPlayerId();
        if (pid < 0) {
            ESP_LOGW(TAG, "No free player slot, disconnecting");
            esp_hidh_dev_close(dev);
            break;
        }
        auto& ctx = self.m_devices[pid];
        ctx.dev = dev;
        ctx.playerId = pid;
        ctx.connected = true;
        if (name) strncpy(ctx.name, name, sizeof(ctx.name) - 1);
        if (bda) memcpy(ctx.bda, bda, 6);

        ESP_LOGI(TAG, "Assigned playerId=%d", pid);
        esp_hidh_dev_dump(dev, stdout);
        break;
    }
    case ESP_HIDH_CLOSE_EVENT: {
        auto* dev = param->close.dev;
        int pid = self.playerIdByDev(dev);
        if (pid >= 0) {
            ESP_LOGI(TAG, "CLOSE: playerId=%d", pid);
            self.m_devices[pid] = {}; // 清空
        }
        esp_hidh_dev_free(dev);
        break;
    }
    case ESP_HIDH_INPUT_EVENT: {
        if (param->input.length == 0) break;
        int pid = self.playerIdByDev(param->input.dev);
        if (pid < 0) break;

        GamepadState state{};
        const uint8_t* d = param->input.data;
        size_t len = param->input.length;

        // 通用 HID Gamepad 解析
        if (len >= 1) state.buttons = d[0];
        if (len >= 2) state.buttons |= (uint16_t(d[1]) << 8);
        state.dpad = d[0] & 0x0F;
        if (state.dpad > 8) state.dpad = 15;
        if (len >= 4) { state.lx = d[2]; state.ly = d[3]; }
        if (len >= 6) { state.rx = d[4]; state.ry = d[5]; }
        if (len >= 8) { state.lt = d[6]; state.rt = d[7]; }

        // 入队
        GamepadInputEvent evt{ static_cast<uint8_t>(pid), state };
        xQueueSend(self.m_inputQueue, &evt, 0);
        break;
    }
    case ESP_HIDH_BATTERY_EVENT: {
        const uint8_t* bda = esp_hidh_dev_bda_get(param->battery.dev);
        ESP_LOGI(TAG, "BATTERY: [%s] %d%%",
                 bda ? (const char*)esp_hidh_dev_name_get(param->battery.dev) : "?", param->battery.level);
        break;
    }
    default:
        break;
    }
}

// NimBLE GAP 事件（扫描发现）
int bleGapEventCb(struct ble_gap_event* event, void* /*arg*/)
{
    auto& self = BleGamepad::instance();

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        // 检查是否为 HID 设备 — 找 HID Service UUID (0x1812) 或游戏手柄 appearance
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data) != 0)
            return 0;

        bool isHid = false;
        if (fields.appearance_is_present) {
            // 0x03C4 = HID Gamepad
            isHid = (fields.appearance >= 0x03C0 && fields.appearance <= 0x03C9);
        }
        if (!isHid && fields.num_uuids16 > 0) {
            for (int i = 0; i < fields.num_uuids16; i++) {
                if (fields.uuids16[i].value == 0x1812) { // HID Service
                    isHid = true;
                    break;
                }
            }
        }
        if (!isHid) return 0;

        // 收集设备信息
        ScanDevice dev;
        memcpy(dev.bda, event->disc.addr.val, 6);
        dev.addrType = event->disc.addr.type;
        dev.rssi = event->disc.rssi;
        dev.appearance = fields.appearance;
        if (fields.name_len > 0) {
            size_t cpLen = std::min<size_t>(fields.name_len, sizeof(dev.name) - 1);
            memcpy(dev.name, fields.name, cpLen);
            dev.name[cpLen] = '\0';
        } else {
            snprintf(dev.name, sizeof(dev.name), "HID_%02X%02X%02X",
                     dev.bda[3], dev.bda[4], dev.bda[5]);
        }

        {
            if (self.m_mutex.try_lock()) {
                // 去重: 相同 BDA 不重复添加
                bool dup = false;
                for (auto& d : self.m_scanResults) {
                    if (memcmp(d.bda, dev.bda, 6) == 0) {
                        d.rssi = dev.rssi; // 更新 RSSI
                        dup = true;
                        break;
                    }
                }
                if (!dup) self.m_scanResults.push_back(dev);
                self.m_mutex.unlock();
            }
        }
        ESP_LOGI(TAG, "SCAN: %s, RSSI=%d, appearance=0x%04x",
                 dev.name, dev.rssi, dev.appearance);
        break;
    }
    case BLE_GAP_EVENT_DISC_COMPLETE: {
        ESP_LOGI(TAG, "Scan complete, found %zu devices", self.m_scanResults.size());
        // 扫描自动重新开始（持续模式）
        if (self.m_scanning) {
            struct ble_gap_disc_params params{};
            params.filter_policy = BLE_HCI_CONN_FILT_NO_WL;
            params.passive = 1;
            params.itvl = 0;
            params.window = 0;
            params.filter_duplicates = 0;
            ble_gap_disc(BLE_OWN_ADDR_PUBLIC, 0, &params, bleGapEventCb, nullptr);
        }
        break;
    }
    default:
        break;
    }
    return 0;
}

// ── NimBLE Host 任务 ──
void BleGamepad::hostTask(void* /*arg*/)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run(); // 阻塞直到 nimble_port_stop()
    nimble_port_freertos_deinit();
}

// ── 输入处理任务 ──
void BleGamepad::processTask(void* arg)
{
    auto& self = *static_cast<BleGamepad*>(arg);
    GamepadInputEvent evt;

    while (true) {
        if (xQueueReceive(self.m_inputQueue, &evt, portMAX_DELAY) == pdTRUE) {
            // 更新设备状态
            if (evt.playerId < MAX_PLAYERS) {
                auto& ctx = self.m_devices[evt.playerId];
                ctx.state = evt.state;
                // 状态变化日志（调试用）
                if (ctx.state.buttons || ctx.state.dpad < 8) {
                    ESP_LOGD(TAG, "P%d: btn=0x%04x lx=%d ly=%d rx=%d ry=%d dpad=%d",
                             evt.playerId, evt.state.buttons,
                             evt.state.lx, evt.state.ly,
                             evt.state.rx, evt.state.ry,
                             evt.state.dpad);
                }
            }
        }
    }
}

// ── 初始化 ──

bool BleGamepad::initEspHostedBt()
{
    ESP_LOGI(TAG, "Connecting to co-processor...");
    if (esp_hosted_connect_to_slave() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to slave");
        return false;
    }

    ESP_LOGI(TAG, "Initializing BT controller...");
    if (esp_hosted_bt_controller_init() != ESP_OK) {
        ESP_LOGE(TAG, "BT controller init failed");
        return false;
    }

    ESP_LOGI(TAG, "Enabling BT controller...");
    if (esp_hosted_bt_controller_enable() != ESP_OK) {
        ESP_LOGE(TAG, "BT controller enable failed");
        return false;
    }

    return true;
}

bool BleGamepad::initNimbleAndHidHost()
{
    ESP_LOGI(TAG, "Initializing NimBLE...");
    nimble_port_init();

    // 保存当前回调，设置我们的回调
    // 注意: esp_hidh_init 会直接覆盖 ble_hs_cfg.sync_cb 而不链式保存!
    // 所以必须在 esp_hidh_init() 之后重新设置我们的回调
    // (参考: ESP-IDF v5.5.2 中 esp_ble_hidh_init 无回调链机制)
    ble_hs_cfg.reset_cb = myResetCb;
    ble_hs_cfg.sync_cb = mySyncCb;

    ESP_LOGI(TAG, "Before esp_hidh_init: reset=%p sync=%p",
             (void*)ble_hs_cfg.reset_cb, (void*)ble_hs_cfg.sync_cb);

    // 初始化 HID Host
    esp_hidh_config_t hidConfig = {
        .callback = hidhCallback,
        .event_stack_size = 4096,
        .callback_arg = nullptr,
    };
    if (esp_hidh_init(&hidConfig) != ESP_OK) {
        ESP_LOGE(TAG, "HID Host init failed");
        return false;
    }

    // 诊断：确认 esp_hidh 覆盖了回调
    ESP_LOGI(TAG, "After esp_hidh_init: reset=%p sync=%p",
             (void*)ble_hs_cfg.reset_cb, (void*)ble_hs_cfg.sync_cb);

    // ★ 关键修复：esp_hidh 覆盖了我们的回调，现在重新设回来
    ble_hs_cfg.reset_cb = myResetCb;
    ble_hs_cfg.sync_cb = mySyncCb;
    ESP_LOGI(TAG, "Our callbacks restored: reset=%p sync=%p",
             (void*)ble_hs_cfg.reset_cb, (void*)ble_hs_cfg.sync_cb);

    // 存储配置
    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // 创建输入事件队列
    m_inputQueue = xQueueCreate(INPUT_QUEUE_LEN, sizeof(GamepadInputEvent));
    if (!m_inputQueue) {
        ESP_LOGE(TAG, "Failed to create input queue");
        return false;
    }

    // 创建输入处理任务
    BaseType_t res = xTaskCreatePinnedToCore(
        processTask, "bleGamepad", 4096, this, 5, &m_processTask,
        tskNO_AFFINITY);
    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create process task");
        return false;
    }

    // 启动 NimBLE 任务
    nimble_port_freertos_init(hostTask);

    return true;
}

bool BleGamepad::start()
{
    if (m_running) return true;

    s_instance = this;

    if (!initEspHostedBt()) {
        ESP_LOGE(TAG, "ESP HOSTED BT init failed");
        return false;
    }

    if (!initNimbleAndHidHost()) {
        ESP_LOGE(TAG, "NimBLE/HID Host init failed");
        return false;
    }

    m_running = true;
    ESP_LOGI(TAG, "BleGamepad started");
    return true;
}

void BleGamepad::stop()
{
    if (!m_running) return;

    disconnectAll();

    if (m_processTask) {
        vTaskDelete(m_processTask);
        m_processTask = nullptr;
    }
    if (m_inputQueue) {
        vQueueDelete(m_inputQueue);
        m_inputQueue = nullptr;
    }

    nimble_port_stop();

    esp_hidh_deinit();
    esp_hosted_bt_controller_disable();
    esp_hosted_bt_controller_deinit(false);

    m_running = false;
    s_instance = nullptr;
    ESP_LOGI(TAG, "BleGamepad stopped");
}

// ── 扫描 ──

void BleGamepad::startScan()
{
    if (!m_running || m_scanning) return;
    m_scanning = true;

    {
        if (m_mutex.try_lock()) {
            m_scanResults.clear();
            m_mutex.unlock();
        }
    }

    // 获取正确的地址类型
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    struct ble_gap_disc_params params{};
    params.filter_policy = 0;
    params.passive = 1;
    params.itvl = 0;
    params.window = 0;
    params.filter_duplicates = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, bleGapEventCb, nullptr);
    if (rc == 0) {
        ESP_LOGI(TAG, "Scan started (own_addr_type=%d)", own_addr_type);
    } else {
        ESP_LOGE(TAG, "Scan start failed: %d (%s)", rc,
                 rc == BLE_HS_EBUSY ? "BUSY" :
                 rc == BLE_HS_ENOTSYNCED ? "NOT SYNCED" :
                 rc == BLE_HS_EALREADY ? "ALREADY" : "OTHER");
        m_scanning = false;
    }
}

void BleGamepad::stopScan()
{
    if (!m_scanning) return;
    m_scanning = false;
    ble_gap_disc_cancel();
    ESP_LOGI(TAG, "Scan stopped");
}

// ── 连接管理 ──

void BleGamepad::connect(uint8_t scanIndex)
{
    ScanDevice dev;
    {
        if (!m_mutex.try_lock()) return;
        if (scanIndex >= m_scanResults.size()) {
            m_mutex.unlock();
            ESP_LOGE(TAG, "Invalid scan index %u", scanIndex);
            return;
        }
        dev = m_scanResults[scanIndex];
        m_mutex.unlock();
    }

    ESP_LOGI(TAG, "Connecting to %s...", dev.name);
    // 检查是否已连
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_devices[i].connected && memcmp(m_devices[i].bda, dev.bda, 6) == 0) {
            ESP_LOGW(TAG, "Already connected to this device");
            return;
        }
    }

    esp_hidh_dev_t* hidDev = esp_hidh_dev_open(dev.bda, ESP_HID_TRANSPORT_BLE, dev.addrType);
    if (!hidDev) {
        ESP_LOGE(TAG, "Failed to open HID device");
    }
}

void BleGamepad::disconnect(uint8_t playerId)
{
    if (playerId >= MAX_PLAYERS) return;
    auto& ctx = m_devices[playerId];
    if (!ctx.connected || !ctx.dev) return;

    ESP_LOGI(TAG, "Disconnecting player %d", playerId);
    esp_hidh_dev_close(static_cast<esp_hidh_dev_t*>(ctx.dev));
}

void BleGamepad::disconnectAll()
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_devices[i].connected) disconnect(i);
    }
}

// ── 查询 ──

uint8_t BleGamepad::connectedCount() const
{
    uint8_t count = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_devices[i].connected) count++;
    }
    return count;
}

const DeviceContext* BleGamepad::getDevice(uint8_t playerId) const
{
    if (playerId >= MAX_PLAYERS) return nullptr;
    return &m_devices[playerId];
}

std::vector<ScanDevice> BleGamepad::getScannedDevices() const
{
    std::vector<ScanDevice> ret;
    if (m_mutex.try_lock()) {
        ret = m_scanResults;
        m_mutex.unlock();
    }
    return ret;
}

// ── 内部 ──

int BleGamepad::allocPlayerId()
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!m_devices[i].connected) return i;
    }
    return -1;
}

int BleGamepad::playerIdByDev(void* dev) const
{
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (m_devices[i].connected && m_devices[i].dev == dev) return i;
    }
    return -1;
}
