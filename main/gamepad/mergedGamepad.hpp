#pragma once

#include <cstdint>
#include <cstddef>
#include "gamepad/gamepadState.hpp"
#include "task/task.hpp"

/**
 * @brief 合并手柄 HID 输出模块（系统底层服务）
 *
 * 将两台 BLE 手柄的输入按固定左右角色合并为一个标准 HID 游戏手柄，
 * 通过 P4 自身的 BLE Peripheral 广播让 PC 识别为完整游戏手柄。
 *
 * 报告格式（10 字节）：
 *   Byte 0:   Left buttons  (A/B/X/Y/L3, 5 bits)
 *   Byte 1:   Right buttons (A/B/X/Y/R3, 5 bits)
 *   Byte 2-3: LX, LY (左侧手柄摇杆)
 *   Byte 4-5: RX, RY (右侧手柄摇杆)
 *   Byte 6:   Hat switch (D-pad)
 *   Byte 7-8: LT, RT (扳机)
 *   Byte 9:   Reserved
 *
 * 使用方式：
 *   - BleGamepad::initNimble() 中 host task 启动前调用 init() 注册 GATT 服务
 *   - BleGamepad::onSync() 中调用 startAdvertising()
 *   - BleGamepad::processTask() 中调用 onGamepadInput()
 *   - GAP Peripheral 连接事件调用 onHidConnected() / onHidDisconnected()
 */

struct InputSlot {
    int8_t hostId{-1};   // -1 = 未分配
    int8_t playerId{-1}; // -1 = 未分配, 0-3 = 手柄编号
    bool isSet() const { return hostId >= 0 && playerId >= 0; }
    bool matches(int8_t h, int8_t p) const { return hostId == h && playerId == p; }
    void clear() { hostId = -1; playerId = -1; }
    void set(int8_t h, int8_t p) { hostId = h; playerId = p; }
};

class MergedGamepad {
public:
    static MergedGamepad& instance();

    // ── 生命周期 ──
    /// 注册 HID GATT 服务（NimBLE host task 启动前调用）
    void init();
    /// 从 NVS 读取 left/right 配置
    void loadSettings();
    /// NVS 持久化 left/right 配置
    void saveSettings();

    // ── 广播控制 ──
    void initAdvertisingData();  // NimBLE sync 后调用一次
    void startAdvertising();
    void stopAdvertising();

    // ── 角色管理 ──
    void setLeftPlayer(int8_t hostId, int8_t playerId);
    void setRightPlayer(int8_t hostId, int8_t playerId);
    void clearLeft();
    void clearRight();

    // ── 输入接收（由 BleGamepad processTask 调用）──
    void onGamepadInput(int8_t hostId, int8_t playerId, const GamepadState& state);

    // ── scan 结束通知（由 BleScheduler 调用）──
    void onScanComplete();

    // ── 状态查询 ──
    bool isHidConnected() const { return m_pcConnected; }
    bool isActive() const { return m_left.isSet() || m_right.isSet(); }
    const InputSlot& getLeft() const { return m_left; }
    const InputSlot& getRight() const { return m_right; }

    // ── HID 连接事件（由 GAP 回调调用）──
    void onHidConnected(uint16_t connHandle);
    void onHidDisconnected(uint16_t connHandle);

private:
    MergedGamepad() = default;
    ~MergedGamepad() = default;
    MergedGamepad(const MergedGamepad&) = delete;

    // HID Report 构建与发送
    void buildReport(uint8_t* buf, size_t& len);
    void sendHidReport();
    void updateActiveState();

    // HID 发送 Task
    static void sendTask(void* arg);

    // GAP 广播事件回调
    static int advGapEvent(struct ble_gap_event *event, void *arg);

    // 角色槽位
    InputSlot m_left{};
    InputSlot m_right{};

    // 最新输入状态（由 processTask 写入，sendTask 读取）
    GamepadState m_leftState{};
    GamepadState m_rightState{};

public:
    // HID GATT 特征句柄（供 GATT 服务表引用）
    uint16_t m_hidReportMapHandle{};   // Report Map 特征值句柄
    uint16_t m_hidReportHandle{};      // Report (Input) 特征值句柄
    uint16_t m_hidReportRefHandle{};   // Report Reference 描述符句柄
    uint16_t m_hidInfoHandle{};        // HID Information 特征值句柄
    uint16_t m_hidProtoModeHandle{};   // Protocol Mode 特征值句柄
    uint16_t m_hidCccdHandle{};        // CCCD 句柄

    // NimBLE GATT 回调（供 GATT 服务表引用）
    static int hidSvrAccessCb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);

private:

    // HID 连接状态
    uint16_t m_hidConnHandle{};
    bool m_pcConnected{};
    bool m_advDataInitialized{};  // 广播数据已设置
    bool m_advPendingRequest{};    // 有挂起的广播请求（scan 期间调用 startAdvertising 失败）（NimBLE sync 后一次性）

    // 发送 Task
    Thread m_sendThread{};
};
