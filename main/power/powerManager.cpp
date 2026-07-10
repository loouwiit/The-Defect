#include "power/powerManager.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr char TAG[] = "PowerManager";

// ── RTC memory 区域 ──
static RTC_DATA_ATTR uint32_t s_rtcStorage[8];

PowerManager::RtcData* PowerManager::s_rtcData =
reinterpret_cast<PowerManager::RtcData*>(&s_rtcStorage[0]);

// ════════════════════════════════════════════════════════════════
// 单例
// ════════════════════════════════════════════════════════════════

PowerManager& PowerManager::instance()
{
    static PowerManager s_instance{};
    return s_instance;
}

PowerManager::PowerManager()
{
    detectWakeupCause();
}

// ════════════════════════════════════════════════════════════════
// 唤醒检测
// ════════════════════════════════════════════════════════════════

void PowerManager::detectWakeupCause()
{
    m_wakeupCauses = esp_sleep_get_wakeup_causes();

    if (m_wakeupCauses) {
        m_wakeupBoot = true;
        ESP_LOGI(TAG, "唤醒原因: 0x%08lx", (unsigned long)m_wakeupCauses);
    }
    else {
        m_wakeupBoot = false;
        ESP_LOGI(TAG, "启动原因: 冷启动 (电源复位)");
    }
}

const char* PowerManager::getWakeupReasonStr() const
{
    if (m_wakeupCauses)
        return "Deep-sleep 唤醒";
    return "冷启动";
}

// ════════════════════════════════════════════════════════════════
// RTC data 初始化
// ════════════════════════════════════════════════════════════════

void PowerManager::initRtcData()
{
    if (s_rtcData->magic != RTC_MAGIC) {
        for (auto& v : s_rtcStorage)
            v = 0;
        s_rtcData->magic = RTC_MAGIC;
    }
    s_rtcData->bootCount++;
}

// ════════════════════════════════════════════════════════════════
// 关机
// ════════════════════════════════════════════════════════════════

void PowerManager::prepareShutdown(lv_display_t* lvgl_disp, lv_obj_t* screen)
{
    ESP_LOGW(TAG, "准备关机（Deep-sleep，硬件复位唤醒）");

    initRtcData();
    s_rtcData->shutdownReason = 1;
}

void PowerManager::shutdown()
{
    ESP_LOGW(TAG, "进入 Deep-sleep");

    vTaskDelay(pdMS_TO_TICKS(20));

    // 无唤醒源，靠硬件复位（电源键/RST 引脚）重新开机
    esp_deep_sleep_start();

    while (1) {}
}
