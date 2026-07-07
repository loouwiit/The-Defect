#include "power/powerManager.hpp"
#include "battery/batteryManager.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "ulp_lp_core.h"
#include "ulp_lp_core_lp_adc_shared.h"

/* LP Core 固件二进制（由 CMake ulp_embed_binary 生成，ULP_APP_NAME="ulp"） */
extern const uint8_t ulp_bin_start[] asm("_binary_ulp_bin_start");
extern const uint8_t ulp_bin_end[]   asm("_binary_ulp_bin_end");

/* ── LP Core 共享变量（由构建系统自动生成 ulp.h） ── */
#include "ulp.h"

static constexpr char TAG[] = "PowerManager";

// ── RTC memory 区域 ──
// ESP32-P4 保留的 RTC slow memory 地址范围
// 使用 RTC_DATA_ATTR 将变量放在 Deep-sleep 不丢失的区域
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

    if (m_wakeupCauses & BIT(ESP_SLEEP_WAKEUP_ULP)) {
        m_wakeupBoot = true;
        ESP_LOGI(TAG, "唤醒原因: LP Core (触控唤醒)");
    }
    else if (m_wakeupCauses & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
        m_wakeupBoot = true;
        ESP_LOGI(TAG, "唤醒原因: 定时器 (5s 超时)");

        /* ── 读取 LP Core 的调试数据 ── */
        ESP_LOGI(TAG, "LP Core 调试数据:");
        ESP_LOGI(TAG, "  运行次数: %lu", (unsigned long)ulp_lp_debug_counter);
        ESP_LOGI(TAG, "  最后 ADC: %lu", (unsigned long)ulp_lp_last_adc_raw);
        ESP_LOGI(TAG, "  最小 ADC: %lu", (unsigned long)ulp_lp_min_adc_raw);
        ESP_LOGI(TAG, "  最大 ADC: %lu", (unsigned long)ulp_lp_max_adc_raw);
        ESP_LOGI(TAG, "  唤醒计数: %lu", (unsigned long)ulp_lp_wakeup_count);

        if (ulp_lp_debug_counter == 0) {
            ESP_LOGE(TAG, "→ LP Core 从未运行！固件可能未正确加载或 ULP 配置缺失");
        }
        else if (ulp_lp_last_adc_raw == 0xDEAD) {
            ESP_LOGE(TAG, "→ LP ADC 读取失败！GPIO21 可能未配置为 ADC 功能");
        }
        else {
            ESP_LOGI(TAG, "→ ADC 范围 %lu~%lu，阈值 800",
                (unsigned long)ulp_lp_min_adc_raw,
                (unsigned long)ulp_lp_max_adc_raw);
            if (ulp_lp_min_adc_raw > 800) {
                ESP_LOGW(TAG, "→ 建议: ADC 从未低于阈值！GT911 INT 是否连接到 GPIO21？");
            }
        }
    }
    else {
        m_wakeupBoot = false;
        ESP_LOGI(TAG, "启动原因: 冷启动 (电源复位)");
    }
}

const char* PowerManager::getWakeupReasonStr() const
{
    if (m_wakeupCauses & BIT(ESP_SLEEP_WAKEUP_ULP))
        return "触控唤醒";
    if (m_wakeupCauses & BIT(ESP_SLEEP_WAKEUP_TIMER))
        return "定时器唤醒";
    return "冷启动";
}

// ════════════════════════════════════════════════════════════════
// RTC data 初始化
// ════════════════════════════════════════════════════════════════

void PowerManager::initRtcData()
{
    if (s_rtcData->magic != RTC_MAGIC) {
        // 首次启动或 RTC memory 失效 → 初始化为 0
        for (auto& v : s_rtcStorage)
            v = 0;
        s_rtcData->magic = RTC_MAGIC;
    }
    s_rtcData->bootCount++;
}

// ════════════════════════════════════════════════════════════════
// LP Core 固件加载
// ════════════════════════════════════════════════════════════════

bool PowerManager::initLpCore()
{
    if (m_lpCoreLoaded) {
        ESP_LOGW(TAG, "LP Core 已加载，跳过");
        return true;
    }

    /* 仅加载 LP Core 固件到 LP SRAM（不初始化 LP ADC，避免与 BatteryManager 冲突） */
    esp_err_t ret = ulp_lp_core_load_binary(
        ulp_bin_start,
        (ulp_bin_end - ulp_bin_start)
    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LP Core 固件加载失败: %s", esp_err_to_name(ret));
        return false;
    }

    m_lpCoreLoaded = true;
    ESP_LOGI(TAG, "LP Core 固件已加载到 LP SRAM");
    return true;
}

/**
 * @brief 初始化 LP ADC（仅在关机前调用，此时 BatteryManager 已释放 ADC1）
 */
static bool initLpAdc()
{
    /* 初始化 LP ADC */
    esp_err_t ret = lp_core_lp_adc_init(ADC_UNIT_1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LP ADC 初始化失败: %s", esp_err_to_name(ret));
        return false;
    }

    /* 配置 LP ADC 通道 (GPIO21 = ADC1_CH5) */
    const lp_core_lp_adc_chan_cfg_t adcCfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = lp_core_lp_adc_config_channel(ADC_UNIT_1, ADC_CHANNEL_5, &adcCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LP ADC 通道配置失败: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "LP ADC 已初始化，通道 ADC1_CH5 (GPIO21)");
    return true;
}

// ════════════════════════════════════════════════════════════════
// 关机
// ════════════════════════════════════════════════════════════════

void PowerManager::prepareShutdown(lv_display_t* lvgl_disp, lv_obj_t* screen)
{
    ESP_LOGW(TAG, "准备关机...");

    // ── 释放 BatteryManager 的 ADC1 句柄 ──
    // LP Core 也需要 ADC1，必须先释放，否则 LP ADC 初始化会冲突
    BatteryManager::instance().deinit();

    // ── 初始化 LP ADC（现在 ADC1 空闲） ──
    if (!initLpAdc()) {
        ESP_LOGE(TAG, "LP ADC 初始化失败，关机仍将继续但触控唤醒可能不可用");
    }

    // ── 保存 RTC data ──
    initRtcData();
    s_rtcData->shutdownReason = 1;

    // ── 确保 LP Core 已加载并运行 ──
    if (!m_lpCoreLoaded) {
        initLpCore();
    }

    /* 启动 LP Core（配置 LP 定时器 10ms 周期唤醒） */
    ulp_lp_core_cfg_t lpCfg = {
        .wakeup_source = ULP_LP_CORE_WAKEUP_SOURCE_LP_TIMER,
        .lp_timer_sleep_duration_us = 10000,  // 10ms
    };

    esp_err_t ret = ulp_lp_core_run(&lpCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LP Core 启动失败: %s", esp_err_to_name(ret));
    }
    else {
        ESP_LOGI(TAG, "LP Core 已启动 (10ms 周期)，等待触控唤醒");
    }

    // ── 使能 ULP 唤醒源 ──
    esp_sleep_enable_ulp_wakeup();

    // ── 调试：暂时移除定时器回退，测试 ULP 唤醒是否单独有效 ──
    // 如果触屏不能唤醒，说明 esp_sleep_enable_ulp_wakeup() 没生效
    // 如果 30 秒后还未唤醒，说明 ULP 唤醒有问题，按下 RST 重启看日志
}

void PowerManager::shutdown()
{
    ESP_LOGW(TAG, "进入 Deep-sleep (仅 LP Core 触控唤醒，无定时器回退)");

    // 等待串口日志 flush
    vTaskDelay(pdMS_TO_TICKS(20));

    // 进入 Deep-sleep
    // LP Core 持续运行，LP ADC 每隔 10ms 采样一次 GPIO21
    // 检测到触控 → LP Core 唤醒 HP CPU
    // 回退：5 秒后 RTC 定时器也会唤醒
    esp_deep_sleep_start();

    // 不会执行到这里
    while (1) {}
}
