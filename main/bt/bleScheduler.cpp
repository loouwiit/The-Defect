#include "bleScheduler.hpp"
#include "gamepad/mergedGamepad.hpp"

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static constexpr char TAG[] = "BleScheduler";

// ══════════════════════════════════════════════════════════════
//  单例
// ══════════════════════════════════════════════════════════════

BleScheduler& BleScheduler::instance()
{
    static BleScheduler s_instance{};
    return s_instance;
}

// ══════════════════════════════════════════════════════════════
//  内部：扫描回调包装器
//  拦截 DISC_COMPLETE 事件，通知调度器扫描结束，然后透传给用户回调
// ══════════════════════════════════════════════════════════════

static int scanCbWrapper(struct ble_gap_event* event, void* arg)
{
    auto& self = BleScheduler::instance();

    // 用户回调（先执行，让用户处理 DISC 等事件）
    int rc = 0;
    auto userCb = self.getScanCb();
    if (userCb) {
        rc = userCb(event, self.getScanArg());
    }

    // 扫描结束 → 通知调度器 + MergedGamepad
    if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        ESP_LOGI(TAG, "Scan complete, notifying scheduler");
        self.onScanComplete();
        MergedGamepad::instance().onScanComplete();
    }

    return rc;
}

// ══════════════════════════════════════════════════════════════
//  扫描
// ══════════════════════════════════════════════════════════════

void BleScheduler::startScan(uint32_t durationMs, GapEventCb cb, void* arg)
{
    // 如果当前正在广播，先停止
    if (m_advertising) {
        ESP_LOGI(TAG, "Stopping adv for scan");
        ble_gap_adv_stop();
        m_advertising = false;
    }

    // 保存用户回调
    m_scanCb = cb;
    m_scanArg = arg;

    // 获取地址类型
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        own_addr_type = BLE_OWN_ADDR_PUBLIC;
    }

    // 启动扫描
    struct ble_gap_disc_params params{};
    params.passive = 1;
    params.filter_duplicates = 1;

    TickType_t duration = (durationMs == 0) ? BLE_HS_FOREVER : pdMS_TO_TICKS(durationMs);
    rc = ble_gap_disc(own_addr_type, duration, &params, scanCbWrapper, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        m_scanCb = nullptr;
        m_scanArg = nullptr;
        return;
    }

    m_scanning = true;
    m_state = State::Scanning;
    ESP_LOGI(TAG, "Scan started (duration=%ums)", durationMs);
}

void BleScheduler::stopScan()
{
    if (!m_scanning) return;

    m_scanning = false;
    m_scanCb = nullptr;
    m_scanArg = nullptr;
    ble_gap_disc_cancel();
    ESP_LOGI(TAG, "Scan stopped");

    // 扫描停止 → 尝试恢复挂起的广播
    onScanComplete();
}

// ══════════════════════════════════════════════════════════════
//  广播
// ══════════════════════════════════════════════════════════════

void BleScheduler::requestAdvertising(const struct ble_gap_adv_params* params,
                                       GapEventCb cb, void* arg)
{
    // 保存广播请求
    m_advParams = *params;
    m_advCb = cb;
    m_advArg = arg;
    m_advPending = true;

    // 如果当前空闲，立即启动
    if (m_state == State::Idle) {
        tryStartAdv();
    }
    // 如果正在扫描，等扫描结束后自动恢复
    else if (m_state == State::Scanning) {
        ESP_LOGI(TAG, "Adv request queued (scan in progress)");
    }
    // 如果正在广播，不需要操作（已经在广播了）
}

void BleScheduler::stopAdvertising()
{
    m_advPending = false;
    m_advCb = nullptr;
    m_advArg = nullptr;

    if (m_advertising) {
        ble_gap_adv_stop();
        m_advertising = false;
        m_state = State::Idle;
        ESP_LOGI(TAG, "Advertising stopped");
    }
}

void BleScheduler::tryStartAdv()
{
    if (!m_advPending) return;

    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                            &m_advParams, m_advCb, m_advArg);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        return;
    }

    m_advertising = true;
    m_advPending = false;
    m_state = State::Advertising;
    ESP_LOGI(TAG, "Advertising started");
}

// ══════════════════════════════════════════════════════════════
//  事件通知
// ══════════════════════════════════════════════════════════════

void BleScheduler::onScanComplete()
{
    m_scanning = false;
    m_scanCb = nullptr;
    m_scanArg = nullptr;

    // 如果有挂起的广播请求，恢复广播
    if (m_advPending) {
        if (m_deferAdvResume) {
            // auto-connect 模式：先等 connect 完成，再恢复广播
            ESP_LOGI(TAG, "Scan done, adv resume deferred (auto-connect)");
            m_state = State::Idle;
        } else {
            ESP_LOGI(TAG, "Scan done, resuming pending adv");
            m_state = State::Idle;
            tryStartAdv();
        }
    } else {
        m_state = State::Idle;
    }
}

void BleScheduler::resumePendingAdv()
{
    m_deferAdvResume = false;
    if (m_advPending && m_state == State::Idle && !m_advertising) {
        ESP_LOGI(TAG, "Deferred adv now resuming");
        tryStartAdv();
    }
}

void BleScheduler::onAdvStopped()
{
    m_advertising = false;
    m_state = State::Idle;
}
