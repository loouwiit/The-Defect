#pragma once

#include <cstdint>
#include "lvgl.h"

/**
 * @brief 电源管理器（单例）
 *
 * 统一管理 Deep-sleep 关机流程。
 * - 关机前: 关闭背光、显示睡眠
 * - 保存关机原因到 RTC memory
 * - 进入 Deep-sleep，靠硬件复位/上电唤醒
 */
class PowerManager
{
public:
    static PowerManager& instance();

    /**
     * @brief 关机前准备
     *
     * 1. 关闭背光
     * 2. 送 MIPI DSI sleep 命令
     * 3. 保存关机标志到 RTC memory
     */
    void prepareShutdown(lv_display_t* lvgl_disp, lv_obj_t* screen);

    /**
     * @brief 进入 Deep-sleep（不会返回）
     */
    [[noreturn]] void shutdown();

    /**
     * @brief 是否是 Deep-sleep 唤醒启动
     */
    bool isWakeupBoot() const { return m_wakeupBoot; }

    /**
     * @brief 获取唤醒原因描述字符串
     */
    const char* getWakeupReasonStr() const;

private:
    PowerManager();
    ~PowerManager() = default;

    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;

    struct RtcData {
        uint32_t magic;
        uint32_t bootCount;
        uint32_t shutdownReason;
    };
    static RtcData* s_rtcData;

    static constexpr uint32_t RTC_MAGIC = 0x504D5044;

    bool m_wakeupBoot{};
    uint32_t m_wakeupCauses{};

    void detectWakeupCause();
    void initRtcData();
};
