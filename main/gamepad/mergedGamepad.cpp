#include "mergedGamepad.hpp"
#include "bleGamepad.hpp"
#include "bt/bleScheduler.hpp"

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

// NimBLE
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "task/task.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr char TAG[] = "MergedGamepad";

// ══════════════════════════════════════════════════════════════
//  HID Report Descriptor — 合并双手柄游戏手柄
//  报告格式 (10 字节，不含 Report ID):
//    byte 0:  Left buttons  (A/B/X/Y/L3, 5 bits) + padding(3)
//    byte 1:  Right buttons (A/B/X/Y/R3, 5 bits) + padding(3)
//    byte 2:  LX axis (int8, -127~+127)
//    byte 3:  LY axis (int8, -127~+127)
//    byte 4:  RX axis (int8, -127~+127)
//    byte 5:  RY axis (int8, -127~+127)
//    byte 6:  Hat switch (uint4, 0-8=方向, 15=neutral)
//    byte 7:  LT (uint8, 0~255)
//    byte 8:  RT (uint8, 0~255)
//    byte 9:  Reserved (1 byte)
// ══════════════════════════════════════════════════════════════

static const uint8_t s_hidReportMap[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)

    // ── Left buttons (5 bits: A/B/X/Y/L3) ──
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (0x01)
    0x29, 0x05,        //   Usage Maximum (0x05)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x05,        //   Report Count (5)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // ── Right buttons (5 bits: A/B/X/Y/R3) ──
    0x19, 0x06,        //   Usage Minimum (0x06)
    0x29, 0x0A,        //   Usage Maximum (0x0A)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x05,        //   Report Count (5)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // ── Padding (6 bits to byte boundary) ──
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x03,        //   Input (Cnst,Var,Abs)

    // ── Left Stick X, Y (8-bit signed each) ──
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x15, 0x81,        //   Logical Minimum (-127)
    0x25, 0x7F,        //   Logical Maximum (127)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // ── Right Stick X, Y (8-bit signed each) ──
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x15, 0x81,        //   Logical Minimum (-127)
    0x25, 0x7F,        //   Logical Maximum (127)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // ── Hat Switch (4-bit, 0-8=方向, 15=neutral) ──
    0x09, 0x39,        //   Usage (Hat Switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x0F,        //   Logical Maximum (15)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)

    // ── Padding (4 bits) ──
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Cnst,Var,Abs)

    // ── Left Trigger (8-bit unsigned) ──
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x32,        //   Usage (Z)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // ── Right Trigger (8-bit unsigned) ──
    0x09, 0x35,        //   Usage (Rz)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // ── Reserved (1 byte) ──
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Cnst,Var,Abs)

    0xC0               // End Collection
};

// ══════════════════════════════════════════════════════════════
//  GATT 服务定义
// ══════════════════════════════════════════════════════════════

// HID Service UUID: 0x1812
static const ble_uuid16_t s_hid_svc_uuid = BLE_UUID16_INIT(0x1812);

// 特征 UUID
static const ble_uuid16_t s_hid_report_map_uuid = BLE_UUID16_INIT(0x2A4B);  // Report Map
static const ble_uuid16_t s_hid_report_uuid    = BLE_UUID16_INIT(0x2A4D);  // Report
static const ble_uuid16_t s_hid_info_uuid      = BLE_UUID16_INIT(0x2A4A);  // HID Information
static const ble_uuid16_t s_hid_proto_uuid     = BLE_UUID16_INIT(0x2A4E);  // Protocol Mode

// 描述符 UUID
static const ble_uuid16_t s_report_ref_uuid = BLE_UUID16_INIT(0x2908);  // Report Reference
static const ble_uuid16_t s_ccc_uuid        = BLE_UUID16_INIT(0x2902);  // CCCD

// HID Information 值
static const uint8_t s_hid_info_value[] = {
    0x11, 0x01,  // bcdHID: 1.11
    0x00,        // Country Code: 0 (not localized)
    0x01         // Flags: normally connectable (bit 1)
};

// Protocol Mode 值
static const uint8_t s_hid_proto_mode_value[] = { 0x01 };  // Report Protocol Mode

// Report Reference 描述符值
static const uint8_t s_report_ref_value[] = {
    0x01,  // Report ID: 1
    0x01   // Report Type: Input
};

// ══════════════════════════════════════════════════════════════
//  GATT 服务表
//  ⚠ Report 特征必须带 Report Reference (0x2908) 描述符，
//    否则 Linux 无法识别为 HID 设备（/dev/input/js0 不出现）。
//    CCCD 由 NimBLE 自动管理（BLE_GATT_CHR_F_NOTIFY 自动创建）。
// ══════════════════════════════════════════════════════════════

// 描述符：Report Reference（告诉主机这是 Report ID=1 的 Input 报告）
static const struct ble_gatt_dsc_def s_report_ref_dsc = {
    .uuid = &s_report_ref_uuid.u,
    .att_flags = BLE_ATT_F_READ,
    .min_key_size = 0,
    .access_cb = MergedGamepad::hidSvrAccessCb,
    .arg = nullptr,
};

// 特征数组必须为 static，否则 compound literal 局部作用域导致悬空指针
static const struct ble_gatt_chr_def s_hid_chars[] = {
    // ── Report Map (Read Only) ──
    {
        .uuid = &s_hid_report_map_uuid.u,
        .access_cb = MergedGamepad::hidSvrAccessCb,
        .arg = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .val_handle = &MergedGamepad::instance().m_hidReportMapHandle,
    },
    // ── HID Information (Read Only) ──
    {
        .uuid = &s_hid_info_uuid.u,
        .access_cb = MergedGamepad::hidSvrAccessCb,
        .arg = nullptr,
        .flags = BLE_GATT_CHR_F_READ,
        .val_handle = &MergedGamepad::instance().m_hidInfoHandle,
    },
    // ── Protocol Mode (Read/Write) ──
    {
        .uuid = &s_hid_proto_uuid.u,
        .access_cb = MergedGamepad::hidSvrAccessCb,
        .arg = nullptr,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        .val_handle = &MergedGamepad::instance().m_hidProtoModeHandle,
    },
    // ── Report (Input, Read + Notify) + Report Reference ──
    // CCCD (0x2902) 由 NimBLE 自动创建（BLE_GATT_CHR_F_NOTIFY 触发）
    // ⚠ ble_gatt_chr_def 字段顺序: uuid → access_cb → arg → descriptors → flags → min_key_size → val_handle
    {
        .uuid = &s_hid_report_uuid.u,
        .access_cb = MergedGamepad::hidSvrAccessCb,
        .arg = nullptr,
        .descriptors = (struct ble_gatt_dsc_def[]) {
            s_report_ref_dsc,
            { 0 },  // end
        },
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &MergedGamepad::instance().m_hidReportHandle,
    },
    { 0 }  // end
};

static const struct ble_gatt_svc_def s_hid_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_hid_svc_uuid.u,
        .includes = nullptr,
        .characteristics = s_hid_chars,
    },
    { 0 }  // end
};

// ══════════════════════════════════════════════════════════════
//  单例
// ══════════════════════════════════════════════════════════════

MergedGamepad& MergedGamepad::instance()
{
    static MergedGamepad s_instance{};
    return s_instance;
}

// ══════════════════════════════════════════════════════════════
//  GATT 访问回调
// ══════════════════════════════════════════════════════════════

int MergedGamepad::hidSvrAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // ── CCCD 写入（客户端使能通知）──
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_DSC) {
        ESP_LOGI(TAG, "HID CCCD written by conn=%u", conn_handle);
        return 0;
    }

    // ── 描述符读取（Report Reference）──
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        if (ble_uuid_cmp(ctxt->dsc->uuid, &s_report_ref_uuid.u) == 0) {
            return os_mbuf_append(ctxt->om, s_report_ref_value, sizeof(s_report_ref_value));
        }
        ESP_LOGW(TAG, "Unknown descriptor read, attr_handle=%u", attr_handle);
        return 0;
    }

    // ── 特征读取（用 UUID 路由，不依赖 val_handle）──
    if (ctxt->op == BLE_ATT_ACCESS_OP_READ) {
        auto* uuid = ctxt->chr->uuid;
        if (ble_uuid_cmp(uuid, &s_hid_report_map_uuid.u) == 0) {
            return os_mbuf_append(ctxt->om, s_hidReportMap, sizeof(s_hidReportMap));
        }
        if (ble_uuid_cmp(uuid, &s_hid_info_uuid.u) == 0) {
            return os_mbuf_append(ctxt->om, s_hid_info_value, sizeof(s_hid_info_value));
        }
        if (ble_uuid_cmp(uuid, &s_hid_proto_uuid.u) == 0) {
            return os_mbuf_append(ctxt->om, s_hid_proto_mode_value, sizeof(s_hid_proto_mode_value));
        }
        ESP_LOGW(TAG, "Unknown chr read, attr_handle=%u", attr_handle);
    }

    return 0;
}

// ══════════════════════════════════════════════════════════════
//  GAP 广播事件回调
// ══════════════════════════════════════════════════════════════

int MergedGamepad::advGapEvent(struct ble_gap_event *event, void *arg)
{
    auto& self = instance();

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            // PC 连接成功
            ESP_LOGI(TAG, "HID Peripheral connected: conn_handle=%u", event->connect.conn_handle);
            self.onHidConnected(event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "HID Peripheral connect failed: %d", event->connect.status);
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "HID Peripheral disconnected: reason=%d", event->disconnect.reason);
        self.onHidDisconnected(event->disconnect.conn.conn_handle);
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "HID CCCD subscribe: attr_handle=%u", event->subscribe.attr_handle);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "HID MTU updated: %u", event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════
//  初始化
// ══════════════════════════════════════════════════════════════

void MergedGamepad::init()
{
    ESP_LOGI(TAG, "Registering HID GATT services...");

    // 注册 HID GATT 服务到 NimBLE（必须在 host task 启动前调用）
    int rc = ble_gatts_count_cfg(s_hid_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(s_hid_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "HID GATT services registered");
}

// ══════════════════════════════════════════════════════════════
//  NVS 持久化
// ══════════════════════════════════════════════════════════════

static constexpr char NVS_NAMESPACE[] = "merged_gp";

void MergedGamepad::loadSettings()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No NVS settings found (err=%d), defaults applied", err);
        return;
    }

    InputSlot slots[2];
    size_t blobSize = sizeof(slots);
    err = nvs_get_blob(handle, "slots", slots, &blobSize);
    if (err == ESP_OK && blobSize == sizeof(slots)) {
        m_left = slots[0];
        m_right = slots[1];
        ESP_LOGI(TAG, "Loaded: left=(%d,%d) right=(%d,%d)",
                 m_left.hostId, m_left.playerId,
                 m_right.hostId, m_right.playerId);
    }

    nvs_close(handle);

    // 恢复后检查活跃状态
    updateActiveState();
}

void MergedGamepad::saveSettings()
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return;
    }

    InputSlot slots[2] = { m_left, m_right };
    err = nvs_set_blob(handle, "slots", slots, sizeof(slots));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set blob failed: %d", err);
    }
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "Settings saved: left=(%d,%d) right=(%d,%d)",
             m_left.hostId, m_left.playerId,
             m_right.hostId, m_right.playerId);
}

// ══════════════════════════════════════════════════════════════
//  角色管理
// ══════════════════════════════════════════════════════════════

void MergedGamepad::setLeftPlayer(int8_t hostId, int8_t playerId)
{
    m_left.set(hostId, playerId);
    updateActiveState();
    saveSettings();
    ESP_LOGI(TAG, "Left set to H%d P%d", hostId, playerId);
}

void MergedGamepad::setRightPlayer(int8_t hostId, int8_t playerId)
{
    m_right.set(hostId, playerId);
    updateActiveState();
    saveSettings();
    ESP_LOGI(TAG, "Right set to H%d P%d", hostId, playerId);
}

void MergedGamepad::clearLeft()
{
    m_left.clear();
    m_leftState = GamepadState{};
    updateActiveState();
    saveSettings();
    ESP_LOGI(TAG, "Left cleared");
}

void MergedGamepad::clearRight()
{
    m_right.clear();
    m_rightState = GamepadState{};
    updateActiveState();
    saveSettings();
    ESP_LOGI(TAG, "Right cleared");
}

void MergedGamepad::updateActiveState()
{
    if (isActive() && !m_pcConnected) {
        startAdvertising();
    } else if (!isActive() && m_pcConnected) {
        stopAdvertising();
    }
}

// ══════════════════════════════════════════════════════════════
//  广播控制
// ══════════════════════════════════════════════════════════════

void MergedGamepad::initAdvertisingData()
{
    // 设置广播数据（必须在 scan 启动前调用）
    // ⚠ BLE 主广播数据上限 31 字节，UUID16 放 scan response（与 gamepad 项目一致）
    //   主广播：flags(3) + tx_pwr(3) + appearance(4) + name(15) = 25 字节 ✓
    struct ble_hs_adv_fields fields{};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.appearance = 0x03C4;
    fields.appearance_is_present = 1;

    static constexpr const char* ADV_NAME = "ESP32 Gamepad";
    fields.name = (uint8_t*)ADV_NAME;
    fields.name_len = strlen(ADV_NAME);
    fields.name_is_complete = 1;

    // 注意：UUID16 (0x1812) 不放主广播（超出 31 字节限制），放 scan response

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    // Scan Response 包含 HID Service UUID
    struct ble_hs_adv_fields rsp_fields{};
    rsp_fields.uuids16 = (ble_uuid16_t[]) { BLE_UUID16_INIT(0x1812) };
    rsp_fields.num_uuids16 = 1;
    rsp_fields.uuids16_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp_fields);

    m_advDataInitialized = true;
    ESP_LOGI(TAG, "HID advertising data set");
}

void MergedGamepad::startAdvertising()
{
    if (!isActive()) return;

    auto& sched = BleScheduler::instance();
    if (sched.isAdvertising() || sched.hasPendingAdv()) return;

    // 广播数据必须由 initAdvertisingData() 在 onSync() 中预先设置
    if (!m_advDataInitialized) {
        // scan 期间无法设置，标记挂起等待 scan 结束后重试
        m_advPendingRequest = true;
        return;
    }

    // 通过调度器请求广播（scan 空闲时执行 ble_gap_adv_start）
    struct ble_gap_adv_params adv_params{};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.itvl_min = 0x20;
    adv_params.itvl_max = 0x40;
    adv_params.channel_map = 0x07;

    sched.requestAdvertising(&adv_params, advGapEvent, nullptr);
    ESP_LOGI(TAG, "HID adv requested via scheduler");
}

void MergedGamepad::stopAdvertising()
{
    BleScheduler::instance().stopAdvertising();

    // 如果 PC 仍连接，断开
    if (m_pcConnected && m_hidConnHandle != 0xFFFF) {
        int rc = ble_gap_terminate(m_hidConnHandle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGW(TAG, "terminate failed: %d", rc);
        }
    }
}

void MergedGamepad::onScanComplete()
{
    // scan 结束后启动广播（数据已由 initAdvertisingData 设置）
    if (m_advPendingRequest && isActive() && !m_pcConnected && m_advDataInitialized) {
        m_advPendingRequest = false;
        startAdvertising();
    }
}

// ══════════════════════════════════════════════════════════════
//  HID 连接事件
// ══════════════════════════════════════════════════════════════

void MergedGamepad::onHidConnected(uint16_t connHandle)
{
    m_hidConnHandle = connHandle;
    m_pcConnected = true;

    // 更新连接参数（低延迟 HID）
    struct ble_gap_upd_params params = {
        .itvl_min = 6,              // 7.5ms (1.25ms units)
        .itvl_max = 12,             // 15ms
        .latency = 0,
        .supervision_timeout = 100, // 1000ms (10ms units)
        .min_ce_len = 4,
        .max_ce_len = 8,
    };
    int rc = ble_gap_update_params(connHandle, &params);
    if (rc != 0) {
        ESP_LOGW(TAG, "update params failed: %d", rc);
    }

    // 启动发送 Task
    m_sendThread = Thread{ sendTask, "mergedHid", this,
                            Thread::Priority::High, 4096 };

    ESP_LOGI(TAG, "PC HID connected, send task started");
}

void MergedGamepad::onHidDisconnected(uint16_t connHandle)
{
    if (connHandle == m_hidConnHandle) {
        m_pcConnected = false;
        m_hidConnHandle = 0xFFFF;

        // 等待发送 Task 退出
        m_sendThread = {};

        ESP_LOGI(TAG, "PC HID disconnected");

        // 如果仍有角色分配，重新广播
        if (isActive()) {
            startAdvertising();
        }
    }
}

// ══════════════════════════════════════════════════════════════
//  输入接收
// ══════════════════════════════════════════════════════════════

void MergedGamepad::onGamepadInput(int8_t hostId, int8_t playerId, const GamepadState& state)
{
    if (m_left.matches(hostId, playerId)) {
        m_leftState = state;
    }
    else if (m_right.matches(hostId, playerId)) {
        m_rightState = state;
    }
    // 不在此触发 updateActiveState — 角色变更由 setLeftPlayer/setRightPlayer 处理
}

// ══════════════════════════════════════════════════════════════
//  HID 报告构建与发送
// ══════════════════════════════════════════════════════════════

// D-pad 值映射：GamepadState dpad (0-7=方向, 15=释放) → HID hat (0-8=方向, 15=neutral)
static uint8_t mapDpadToHat(uint8_t dpad)
{
    // GamepadState dpad: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 15=none
    // HID hat:          0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 15=none
    // 映射完全一致（0-7 对应 0-7，15 对应 15）
    if (dpad <= 7 || dpad == 15) return dpad;
    return 15;  // 其他值视为释放
}

void MergedGamepad::buildReport(uint8_t* buf, size_t& len)
{
    // 左侧按钮：A(bit0) B(bit1) X(bit3) Y(bit4) L3(bit13) → 5 bits
    uint8_t leftBtns = 0;
    if (m_left.isSet()) {
        if (m_leftState.isPressed(BTN_A))      leftBtns |= (1 << 0);
        if (m_leftState.isPressed(BTN_B))      leftBtns |= (1 << 1);
        if (m_leftState.isPressed(BTN_X))      leftBtns |= (1 << 2);
        if (m_leftState.isPressed(BTN_Y))      leftBtns |= (1 << 3);
        if (m_leftState.isPressed(BTN_L3))     leftBtns |= (1 << 4);
    }

    // 右侧按钮：A(bit0) B(bit1) X(bit3) Y(bit4) L3(bit4→R3) → 5 bits
    uint8_t rightBtns = 0;
    if (m_right.isSet()) {
        if (m_rightState.isPressed(BTN_A))     rightBtns |= (1 << 0);
        if (m_rightState.isPressed(BTN_B))     rightBtns |= (1 << 1);
        if (m_rightState.isPressed(BTN_X))     rightBtns |= (1 << 2);
        if (m_rightState.isPressed(BTN_Y))     rightBtns |= (1 << 3);
        if (m_rightState.isPressed(BTN_L3))    rightBtns |= (1 << 4);  // L3→R3
    }

    // 摇杆：GamepadState 0-255 → HID signed -127~+127
    int8_t lx = m_left.isSet()  ? (int8_t)(m_leftState.lx - 128)  : 0;
    int8_t ly = m_left.isSet()  ? (int8_t)(m_leftState.ly - 128)  : 0;
    int8_t rx = m_right.isSet() ? (int8_t)(m_rightState.lx - 128) : 0;  // 右侧手柄的物理左摇杆 → HID 右摇杆
    int8_t ry = m_right.isSet() ? (int8_t)(m_rightState.ly - 128) : 0;

    // Hat switch：优先取左侧
    uint8_t hat = 15;  // neutral
    if (m_left.isSet()) {
        hat = mapDpadToHat(m_leftState.dpad);
    } else if (m_right.isSet()) {
        hat = mapDpadToHat(m_rightState.dpad);
    }

    // Triggers：硬件无模拟扳机值，用按钮是否按下代替
    uint8_t lt = m_left.isSet() && m_leftState.isPressed(BTN_TL2) ? 255 : 0;
    uint8_t rt = m_right.isSet() && m_rightState.isPressed(BTN_TL2) ? 255 : 0;

    // 打包（10 字节，不含 Report ID）
    buf[0] = leftBtns;                 // Left buttons
    buf[1] = rightBtns;                // Right buttons
    buf[2] = (uint8_t)lx;             // LX
    buf[3] = (uint8_t)ly;             // LY
    buf[4] = (uint8_t)rx;             // RX
    buf[5] = (uint8_t)ry;             // RY
    buf[6] = (hat << 4) | 0x0F;       // Hat (高 4 位) + padding (低 4 位 = 0xF)
    buf[7] = lt;                       // LT
    buf[8] = rt;                       // RT
    buf[9] = 0;                        // Reserved

    len = 10;
}

void MergedGamepad::sendHidReport()
{
    if (!m_pcConnected || m_hidConnHandle == 0xFFFF || m_hidReportHandle == 0)
        return;

    uint8_t report[10];
    size_t len = 0;
    buildReport(report, len);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, len);
    if (om == NULL) {
        ESP_LOGW(TAG, "mbuf alloc failed");
        return;
    }

    int rc = ble_gattc_notify_custom(m_hidConnHandle, m_hidReportHandle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
    }
}

// ══════════════════════════════════════════════════════════════
//  HID 发送 Task（60fps）
// ══════════════════════════════════════════════════════════════

void MergedGamepad::sendTask(void* arg)
{
    auto& self = *static_cast<MergedGamepad*>(arg);

    ESP_LOGI(TAG, "HID send task started");

    while (self.m_pcConnected) {
        self.sendHidReport();
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60fps
    }

    ESP_LOGI(TAG, "HID send task ended");
}
