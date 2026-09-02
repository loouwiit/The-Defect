#pragma once

#include <cstdint>
#include <functional>
#include "host/ble_gap.h"

/**
 * @brief BLE GAP 过程调度器
 *
 * 协调 scan 和 advertise 两个互斥的 GAP 过程：
 * - scan 优先（手柄连接是基础功能）
 * - advertise 在 scan 空闲时执行
 * - scan 请求到来时暂停 advertise，scan 结束后自动恢复
 *
 * NimBLE 同一时间只能执行一个 GAP 过程（scan/adv/connect），
 * 本调度器统一管理 scan 和 adv 请求，避免 BLE_HS_EBUSY 冲突。
 */
class BleScheduler {
public:
    using GapEventCb = int(*)(struct ble_gap_event*, void*);

    static BleScheduler& instance();

    // ── 扫描请求 ──
    /// 启动扫描（如果当前正在广播，先停止广播）
    /// @param durationMs  扫描持续时间（ms），0 = 永久
    /// @param cb          GAP 事件回调（收到 DISC_COMPLETE 时自动标记扫描结束）
    /// @param arg         回调参数
    void startScan(uint32_t durationMs, GapEventCb cb, void* arg = nullptr);

    /// 停止扫描
    void stopScan();

    // ── 广播请求 ──
    /// 请求启动广播（如果当前正在扫描，延迟到扫描结束后执行）
    /// @param params  广播参数
    /// @param cb      GAP 事件回调
    /// @param arg     回调参数
    void requestAdvertising(const struct ble_gap_adv_params* params,
                            GapEventCb cb, void* arg = nullptr);

    /// 停止广播
    void stopAdvertising();

    // ── 状态查询 ──
    bool isScanning() const { return m_scanning; }
    bool isAdvertising() const { return m_advertising; }
    bool hasPendingAdv() const { return m_advPending; }

    // ── 内部回调（由 NimBLE 事件触发）──
    void onScanComplete();
    void onAdvStopped();

    // ── 广播恢复控制 ──
    /// 扫描结束后延迟恢复广播（auto-connect 流程：先 connect 再 adv）
    void setDeferAdvResume(bool defer) { m_deferAdvResume = defer; }
    /// 手动触发挂起的广播恢复（conectNext 完成后调用）
    void resumePendingAdv();

    // ── 扫描回调访问（供 wrapper 使用）──
    GapEventCb getScanCb() const { return m_scanCb; }
    void* getScanArg() const { return m_scanArg; }
    void clearScanCb() { m_scanCb = nullptr; m_scanArg = nullptr; }

private:
    BleScheduler() = default;

    void tryStartAdv();

    enum class State : uint8_t {
        Idle,
        Scanning,
        Advertising,
    };

    State m_state{State::Idle};

    // 扫描状态
    bool m_scanning{false};

    // 广播状态
    bool m_advertising{false};
    bool m_advPending{false};  // 有挂起的广播请求（scan 结束后恢复）
    bool m_deferAdvResume{false};  // 延迟广播恢复（auto-connect：先 connect 再 adv）

    // 广播请求参数（持久保存，scan 结束后恢复时使用）
    struct ble_gap_adv_params m_advParams{};
    GapEventCb m_advCb{nullptr};
    void* m_advArg{nullptr};

    // 扫描回调（透传给调用方）
    GapEventCb m_scanCb{nullptr};
    void* m_scanArg{nullptr};
};
