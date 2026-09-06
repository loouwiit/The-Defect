#include "bleHidDevice.hpp"
#include "mixedInput.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "battery/batteryManager.hpp"
#include "wifi/mdns.hpp"

#include "esp_log.h"
#include "esp_err.h"
#include "esp_hidd.h"
#include "esp_hid_common.h"
#include "esp_event.h"

// NimBLE
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "services/gap/ble_svc_gap.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

// ══════════════════════════════════════════════════════════════
//  HID Report Descriptor — 游戏手柄（混合输入，Xbox 布局）
//  报告格式 (7 bytes):
//    byte 0~1: buttons[15] + padding[1] (uint16, 小端序)
//    byte 2:   X  轴 (int8, -127~+127)
//    byte 3:   Y  轴 (int8, -127~+127)
//    byte 4:   RX 轴 (int8, -127~+127)
//    byte 5:   RY 轴 (int8, -127~+127)
//    byte 6:   Hat(4bit) + padding(4bit)
// ══════════════════════════════════════════════════════════════

static const unsigned char s_gamepadReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)

    // Buttons (15 buttons, 1 bit each = 15 bits, 2 bytes with padding)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x0F,        //   Usage Maximum (0x0F)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0F,        //   Report Count (15)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Padding to 16-bit boundary (1 bit)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Cnst,Var,Abs)

    // X, Y, RX, RY axes (8-bit each, signed -127~127)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x15, 0x81,        //   Logical Minimum (-127)
    0x25, 0x7F,        //   Logical Maximum (127)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Hat Switch (D-Pad)
    0x09, 0x39,        //   Usage (Hat Switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot: Degree)
    0x55, 0x0E,        //   Unit Exponent (-2)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)

    // Padding (4 bits to byte boundary)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Cnst)

    0xC0               // End Collection
};

// ══════════════════════════════════════════════════════════════
//  HID 设备配置
// ══════════════════════════════════════════════════════════════

static esp_hid_raw_report_map_t s_bleReportMaps[] = {
    {
        .data = s_gamepadReportMap,
        .len  = sizeof(s_gamepadReportMap)
    }
};

static constexpr const char* HID_MANUFACTURER = "Espressif";
static constexpr const char* HID_SERIAL       = "P4-00000001";
static constexpr uint16_t    HID_VID          = 0x16C0;
static constexpr uint16_t    HID_PID          = 0x05DF;
static constexpr uint16_t    HID_VERSION      = 0x0100;

static esp_hid_device_config_t s_hidConfig = {
    .vendor_id         = HID_VID,
    .product_id        = HID_PID,
    .version           = HID_VERSION,
    .device_name       = "esp32p4",       // 默认值，init() 中用 mDNS 主机名覆盖
    .manufacturer_name = HID_MANUFACTURER,
    .serial_number     = HID_SERIAL,
    .report_maps       = s_bleReportMaps,
    .report_maps_len   = 1
};

// ══════════════════════════════════════════════════════════════
//  广播参数
// ══════════════════════════════════════════════════════════════

static constexpr uint16_t ADV_APPEARANCE = 0x03C4; // ESP_HID_APPEARANCE_GAMEPAD
static char s_advDeviceName[64] = {};

// ══════════════════════════════════════════════════════════════
//  Singleton
// ══════════════════════════════════════════════════════════════

BleHidDevice& BleHidDevice::instance()
{
    static BleHidDevice inst;
    return inst;
}

// ══════════════════════════════════════════════════════════════
//  HID 事件回调
// ══════════════════════════════════════════════════════════════

static void hidEventHandler(void* /*handler_args*/, esp_event_base_t /*base*/,
                             int32_t event_id, void* event_data)
{
    static constexpr const char* TAG = "BleHidDevice";
    auto& self = BleHidDevice::instance();
    auto event = static_cast<esp_hidd_event_t>(event_id);
    auto* param = static_cast<esp_hidd_event_data_t*>(event_data);

    switch (event)
    {
    case ESP_HIDD_CONNECT_EVENT:
    {
        ESP_LOGI(TAG, "HID connected");
        self.setConnected(true);
        self.startPollTask();
        // 立即用真实电量做首次上报（避免 PC 缓存初始值）
        {
            int bat = BatteryManager::getMinBatteryLevel();
            if (bat >= 0)
                self.setBatteryLevel(static_cast<uint8_t>(bat));
        }
        break;
    }

    case ESP_HIDD_DISCONNECT_EVENT:
    {
        ESP_LOGI(TAG, "HID disconnected (reason=%d)", param->disconnect.reason);
        self.setConnected(false);
        self.stopPollTask();
        // NimBLE 断连时自动停止广播，但 m_advertising 仍为 true，
        // 必须先重置再调用 startAdvertising()，否则早返回检查会跳过
        self.resetAdvertising();
        self.startAdvertising();
        break;
    }

    case ESP_HIDD_PROTOCOL_MODE_EVENT:
        ESP_LOGD(TAG, "Protocol mode: %d", param->protocol_mode.protocol_mode);
        break;

    case ESP_HIDD_CONTROL_EVENT:
        ESP_LOGD(TAG, "Control: %d", param->control.control);
        break;

    case ESP_HIDD_OUTPUT_EVENT:
        ESP_LOGD(TAG, "Output report id=%d len=%d",
                 param->output.report_id, param->output.length);
        break;

    default:
        break;
    }
}

// ══════════════════════════════════════════════════════════════
//  init — 注册 HID GATT 服务
// ══════════════════════════════════════════════════════════════

bool BleHidDevice::init()
{
    if (m_initialized)
    {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing BLE HID Device");

    // 从 NVS 读取设备名称（优先 mDNS 主机名）
    {
        const char* host = mdnsGetConfiguredHostname();
        if (host && host[0] != '\0')
        {
            strncpy(s_advDeviceName, host, sizeof(s_advDeviceName) - 1);
            ESP_LOGI(TAG, "HID device name from mDNS: %s", s_advDeviceName);
        }
        else
        {
            strncpy(s_advDeviceName, "esp32p4", sizeof(s_advDeviceName) - 1);
        }
    }
    s_advDeviceName[sizeof(s_advDeviceName) - 1] = '\0';
    s_hidConfig.device_name = s_advDeviceName;

    // ── 保存 ble_hs_cfg 回调（esp_hidd_dev_init 会覆盖） ──
    m_savedResetCb = ble_hs_cfg.reset_cb;
    m_savedSyncCb  = ble_hs_cfg.sync_cb;

    // ── 注册 HID GATT 服务 ──
    // esp_hidd_dev_init 内部调用 nimble_hid_start_gatts()，
    // 注册 HID/Battery/DIS GATT 服务 + GAP event listener。
    // 会覆盖 ble_hs_cfg.reset_cb / sync_cb / gatts_register_cb。
    esp_err_t ret = esp_hidd_dev_init(&s_hidConfig, ESP_HID_TRANSPORT_BLE,
                                       (esp_event_handler_t)hidEventHandler, &m_hidDev);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_hidd_dev_init failed: %d", ret);
        return false;
    }

    // ── 恢复 BleGamepad 的 ble_hs_cfg 回调 ──
    ble_hs_cfg.reset_cb = m_savedResetCb;
    ble_hs_cfg.sync_cb  = m_savedSyncCb;

    ESP_LOGI(TAG, "BLE HID Device initialized (GATT services registered)");
    m_initialized = true;
    return true;
}

// ══════════════════════════════════════════════════════════════
//  deinit
// ══════════════════════════════════════════════════════════════

void BleHidDevice::deinit()
{
    if (!m_initialized) return;

    stopPollTask();
    stopAdvertising();

    if (m_hidDev)
    {
        esp_hidd_dev_deinit(m_hidDev);
        m_hidDev = nullptr;
    }

    m_connected   = false;
    m_initialized = false;
    ESP_LOGI(TAG, "BLE HID Device deinitialized");
}

// ══════════════════════════════════════════════════════════════
//  广播管理
// ══════════════════════════════════════════════════════════════

bool BleHidDevice::startAdvertising()
{
    if (!m_initialized || m_advertising)
        return m_advertising;

    // ── 设置广播数据 ──
    struct ble_hs_adv_fields adv_fields;
    memset(&adv_fields, 0, sizeof(adv_fields));

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    adv_fields.appearance = ADV_APPEARANCE;
    adv_fields.appearance_is_present = 1;

    adv_fields.name = (uint8_t*)s_advDeviceName;
    adv_fields.name_len = strlen(s_advDeviceName);
    adv_fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return false;
    }

    // ── 设置 Scan Response（含 HID Service UUID） ──
    struct ble_hs_adv_fields rsp_fields;
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    rsp_fields.uuids16 = (ble_uuid16_t[]){
        BLE_UUID16_INIT(0x1812),   // HID Service UUID
        BLE_UUID16_INIT(0x180F)    // Battery Service UUID
    };
    rsp_fields.num_uuids16 = 2;
    rsp_fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return false;
    }

    // ── 设置 GAP Device Name ──
    rc = ble_svc_gap_device_name_set(s_advDeviceName);
    if (rc != 0)
    {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: %d (non-fatal)", rc);
        // 非致命：GAP Name 不影响广播功能
    }

    // ── 启动广播 ──
    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;    // 可连接
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;    // 一般可发现
    adv_params.itvl_min  = BLE_GAP_ADV_ITVL_MS(200);
    adv_params.itvl_max  = BLE_GAP_ADV_ITVL_MS(337);

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                           &adv_params, nullptr, nullptr);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return false;
    }

    m_advertising = true;
    ESP_LOGI(TAG, "HID advertising started: %s", s_advDeviceName);
    return true;
}

void BleHidDevice::stopAdvertising()
{
    if (!m_advertising) return;

    int rc = ble_gap_adv_stop();
    if (rc != 0)
    {
        ESP_LOGW(TAG, "ble_gap_adv_stop failed: %d", rc);
    }

    m_advertising = false;
    ESP_LOGI(TAG, "HID advertising stopped");
}

// ══════════════════════════════════════════════════════════════
//  发送 HID 输入报告
// ══════════════════════════════════════════════════════════════

bool BleHidDevice::sendReport(const uint8_t* data, size_t len)
{
    if (!m_connected || !m_hidDev)
        return false;

    esp_err_t ret = esp_hidd_dev_input_set(m_hidDev, 0, 1, const_cast<uint8_t*>(data), len);
    if (ret != ESP_OK)
    {
        ESP_LOGD(TAG, "sendReport failed: %d", ret);
        return false;
    }
    return true;
}

// ══════════════════════════════════════════════════════════════
//  电池电量
// ══════════════════════════════════════════════════════════════

void BleHidDevice::setBatteryLevel(uint8_t level)
{
    if (m_hidDev)
        esp_hidd_dev_battery_set(m_hidDev, level);
}

// ══════════════════════════════════════════════════════════════
//  输入轮询任务 — 监控 VirtualGamepad 状态，转发到 BLE HID
// ══════════════════════════════════════════════════════════════

void BleHidDevice::startPollTask()
{
    if (m_pollRunning) return;
    m_pollRunning = true;
    TaskHandle_t pollHandle = nullptr;
    xTaskCreate(pollTaskEntry, "bleHidPoll", 4096, this, 5, &pollHandle);
    m_pollTask = pollHandle;
    ESP_LOGI(TAG, "HID poll task started");
}

void BleHidDevice::stopPollTask()
{
    if (!m_pollRunning) return;
    m_pollRunning = false;
    // 等待任务自行退出（检查 m_pollRunning 标志）
    for (int i = 0; i < 50 && m_pollTask != nullptr; i++)
        vTaskDelay(pdMS_TO_TICKS(10));
    m_pollTask = nullptr;
    ESP_LOGI(TAG, "HID poll task stopped");
}

void BleHidDevice::pollTaskEntry(void* arg)
{
    auto& self = *static_cast<BleHidDevice*>(arg);
    self.pollLoop();
    self.m_pollTask = nullptr;
    // 自删：FreeRTOS 回收 TCB + 栈，无需外部 delete
    vTaskDelete(nullptr);
}

void BleHidDevice::pollLoop()
{
    MixedHidState prevMixed{};
    int lastBattery = -1;
    int batteryTick = 0;

    while (m_pollRunning)
    {
        if (m_connected)
        {
            // ── 混合输入转发 ──
            MixedHidState mixed = mixInputs(kDefaultMixedConfig);
            if (mixed != prevMixed)
            {
                uint8_t report[kHidReportSize];
                buildHidReport(mixed, report);
                sendReport(report, kHidReportSize);
                prevMixed = mixed;
            }

            // ── 电量上报（每 30 秒） ──
            if (++batteryTick >= 1875)  // 1875 × 16ms ≈ 30s
            {
                batteryTick = 0;
                int minBat = BatteryManager::getMinBatteryLevel();

                if (minBat >= 0 && minBat != lastBattery)
                {
                    ESP_LOGI(TAG, "Updating battery level: %d%%", minBat);
                    setBatteryLevel(static_cast<uint8_t>(minBat));
                    lastBattery = minBat;
                }
            }
        }
        else
        {
            prevMixed = {};
            batteryTick = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }
}
