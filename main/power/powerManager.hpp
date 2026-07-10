#pragma once

#include <cstdint>
#include "lvgl.h"

/**
 * @brief 电源管理器（单例）
 *
 * 统一管理 Deep-sleep 关机/唤醒流程。
 * - 关机前: LP Core 固件加载、显示睡眠、背光关闭
 * - 唤醒: 检测唤醒原因 (ULP / Timer / 冷启动)
 * - RTC memory 持久化关机原因
 *
 * 使用方式:
 * @code
 *   // main.cpp — 首次冷启动初始化
 *   PowerManager::instance().initLpCore();
 *
 *   // 关机时
 *   PowerManager::instance().prepareShutdown();
 *   PowerManager::instance().shutdown();
 * @endcode
 */
class PowerManager
{
public:
    static PowerManager& instance();

    /**
     * @brief 初始化并加载 LP Core 固件（仅在冷启动时调用）
     * @return true 成功
     */
    bool initLpCore();

    /**
     * @brief 关机前准备
     *
     * 1. 关闭背光
     * 2. 送 MIPI DSI sleep 命令
     * 3. 保存关机标志到 RTC memory
     * 4. 启动 LP Core 固件（触控唤醒检测）
     * 5. 使能 ULP 唤醒源
     */
    void prepareShutdown(lv_display_t* lvgl_disp, lv_obj_t* screen);

    /**
     * @brief 进入 Deep-sleep（不会返回）
     */
    [[noreturn]] void shutdown();

    /**
     * @brief 是否是 ULP (LP Core) 唤醒启动
     */
    bool isWakeupBoot() const { return m_wakeupBoot; }

    /**
     * @brief 获取唤醒原因描述字符串（仅用于日志/显示）
     */
    const char* getWakeupReasonStr() const;

    /**
     * @brief 是否是短周期定时器醒来但无触摸（应重新入睡）
     */
    bool shouldReEnterSleep() const { return m_shouldReSleep; }

    /**
     * @brief 重新进入 Deep-sleep（触摸轮询）
     *
     * 在 shouldReEnterSleep() 返回 true 时调用，
     * 不会执行完整的关机流程，仅重新配置定时器后入睡。
     */
    void reEnterDeepSleep();

    /**
     * @brief 触摸轮询间隔（微秒）
     *
     * HP CPU 以此周期从 Deep-sleep 醒来检查 LP Core 的触摸标志。
     * 低于 50ms 会导致过多时间花在启动上。
     */
    static constexpr uint64_t TouchPollIntervalUs = 2000 * 1000; // 2000ms

private:
    PowerManager();
    ~PowerManager() = default;

    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;

    // ── RTC memory 保留字段（Deep-sleep 唤醒后不丢失） ──
    struct RtcData {
        uint32_t magic;             // 校验用幻数
        uint32_t bootCount;         // 总启动次数
        uint32_t shutdownReason;    // 关机原因
    };
    static RtcData* s_rtcData;

    static constexpr uint32_t RTC_MAGIC = 0x504D5044;
    static constexpr uint32_t RTC_DATA_OFFS = 0;

    // ── 状态 ──
    bool m_wakeupBoot{};
    bool m_shouldReSleep{};
    uint32_t m_wakeupCauses{};
    bool m_lpCoreLoaded{};

    // ── 内部方法 ──
    void detectWakeupCause();
    void initRtcData();
};
