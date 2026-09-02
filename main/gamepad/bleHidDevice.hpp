#pragma once

#include <cstdint>
#include "esp_event.h"

struct esp_hidd_dev_s;
typedef struct esp_hidd_dev_s esp_hidd_dev_t;

/// @brief BLE HID Device — 让 P4 本身作为 HID 游戏手柄被 PC/手机识别
///
/// 使用 esp_hid 组件（NimBLE 后端）注册 HID GATT 服务，
/// 通过 BLE Notification 发送 HID 输入报告。
///
/// 初始化时序（由 BleGamepad 调用）：
///   1. init()        — nimble_port_init() 之后、nimble_port_freertos_init() 之前
///   2. startAdv()    — NimBLE sync_cb 回调中
///   3. sendReport()  — 运行时发送输入
///   4. deinit()      — BleGamepad::stop() 中
class BleHidDevice
{
public:
    /// @brief HID 报告结构体 (4 bytes) — 与 C6 手柄固件对齐
    struct Report
    {
        uint16_t buttons;   // bit0~14: 按钮, bit15: padding
        int8_t x;           // X 轴 (-127 ~ +127)
        int8_t y;           // Y 轴 (-127 ~ +127)
    };

    /// @brief 按钮遮罩 — 与 GamepadButton / C6 HIDGamepad::Button 对齐
    enum Button : uint16_t
    {
        BTN_A      = 1 << 0,
        BTN_B      = 1 << 1,
        BTN_X      = 1 << 3,
        BTN_Y      = 1 << 4,
        BTN_TL     = 1 << 6,   // LB
        BTN_TR     = 1 << 7,   // RB
        BTN_TL2    = 1 << 8,   // LT
        BTN_TR2    = 1 << 9,   // RT
        BTN_SELECT = 1 << 10,
        BTN_START  = 1 << 11,
        BTN_L3     = 1 << 13,
        BTN_R3     = 1 << 14,
    };

    static BleHidDevice& instance();

    /// @brief 初始化 HID 设备（注册 GATT 服务）
    /// @note  必须在 nimble_port_init() 之后、nimble_port_freertos_init() 之前调用
    /// @return true 成功
    bool init();

    /// @brief 反初始化
    void deinit();

    /// @brief 启动 BLE HID 广播
    /// @note  必须在 NimBLE host synced 后调用
    /// @return true 成功
    bool startAdvertising();

    /// @brief 停止广播
    void stopAdvertising();

    /// @brief 发送 HID 输入报告
    /// @return true 成功（已连接且发送成功）
    bool sendReport(const Report& report);

    /// @brief 设置电池电量 (0-100)
    void setBatteryLevel(uint8_t level);

    /// @brief 是否有 PC/手机连接
    bool isConnected() const { return m_connected; }

    /// @brief 是否正在广播
    bool isAdvertising() const { return m_advertising; }

    /// @brief 是否已初始化
    bool isInitialized() const { return m_initialized; }

    /// @brief 获取底层 HID 设备句柄（供事件回调使用）
    esp_hidd_dev_t* hidDev() const { return m_hidDev; }

    /// @brief 设置连接状态（供事件回调使用）
    void setConnected(bool v) { m_connected = v; }

    /// @brief 重置广播状态（供断连回调强制重启广播）
    void resetAdvertising() { m_advertising = false; }

    // 输入轮询任务（事件回调中调用）
    void startPollTask();
    void stopPollTask();

private:
    BleHidDevice() = default;
    ~BleHidDevice() = default;
    BleHidDevice(const BleHidDevice&) = delete;
    BleHidDevice& operator=(const BleHidDevice&) = delete;

    static constexpr const char* TAG = "BleHidDevice";

    bool m_initialized = false;
    bool m_connected   = false;
    bool m_advertising = false;
    esp_hidd_dev_t* m_hidDev = nullptr;

    // ble_hs_cfg 回调备份（esp_hidd_dev_init 会覆盖）
    void (*m_savedResetCb)(int) = nullptr;
    void (*m_savedSyncCb)() = nullptr;

    // 输入轮询任务
    void* m_pollTask = nullptr;
    bool m_pollRunning = false;
    static void pollTaskEntry(void* arg);
    void pollLoop();
};
