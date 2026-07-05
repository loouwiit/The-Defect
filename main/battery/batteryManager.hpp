#pragma once

#include "lvgl.h"
#include <cstdint>
#include "adc_battery_estimation.h"
#include "gpio/gpio.hpp"

/**
 * @brief 主机电池管理器（单例）
 *
 * 封装 ESP32-P4 主机电池的 ADC 读取，
 * 提供电量百分比、电压估算和 LVGL 显示辅助方法。
 *
 * 集中管理 adc_battery_estimation 组件的生命周期，
 * 替代原分散在 desktopApp 和 powerManagementApp 中的重复实现。
 *
 * 使用方式：
 * @code
 *   // main.cpp — 初始化一次
 *   BatteryManager::instance().init();
 *
 *   // 任何需要电池信息的 App
 *   int pct = BatteryManager::instance().getPercent();
 *   int mv  = BatteryManager::instance().getVoltageMv();
 *
 *   // 静态辅助方法（无状态，可随处使用）
 *   lv_label_set_text(label, BatteryManager::getIcon(pct));
 *   lv_obj_set_style_text_color(label, BatteryManager::getColor(pct), 0);
 * @endcode
 */
class BatteryManager
{
public:
	static BatteryManager& instance();

	bool init();
	void deinit();

	/**
	 * @brief 读取主机电池电量百分比
	 * @return 0~100，读取失败返回 -1
	 */
	int getPercent();

	/**
	 * @brief 估算主机电池电压
	 * @return 毫伏 (mV)，读取失败返回 -1
	 */
	int getVoltageMv();

	/**
	 * @brief 读取当前充电状态
	 * @return true=充电中, false=未充电
	 */
	bool isCharging();

	// ── 静态辅助方法（纯函数，无状态依赖） ──

	/** 根据百分比返回 LV_SYMBOL_BATTERY_* 图标 */
	static const char* getIcon(int percent);

	/** 根据百分比返回颜色（≥61% 绿, 21~60% 黄, ≤20% 红） */
	static lv_color_t getColor(int percent);

private:
	BatteryManager() = default;
	~BatteryManager() = default;

	BatteryManager(const BatteryManager&) = delete;
	BatteryManager& operator=(const BatteryManager&) = delete;

	// ── ADC 硬件配置 ──
	static constexpr auto ADC_UNIT         = ADC_UNIT_1;
	static constexpr auto ADC_CHANNEL      = ADC_CHANNEL_6;
	static constexpr auto ADC_ATTEN        = ADC_ATTEN_DB_12;
	static constexpr auto ADC_BITWIDTH     = ADC_BITWIDTH_DEFAULT;
	static constexpr float RESISTOR_UPPER  = 5.1f;  // 上拉电阻 (kΩ)
	static constexpr float RESISTOR_LOWER  = 10.2f; // 下拉电阻 (kΩ)

	// ── 充电检测 ──
	static constexpr auto CHARGING_GPIO    = GPIO_NUM_23;
	static bool chargingDetectCb(void* userData);

	// ── 组件句柄（adc_battery_estimation） ──
	void* m_batteryEstHandle{};

	// ── 自有 ADC 句柄（与组件共享，直接读取电压） ──
	struct AdcHandles {
		adc_oneshot_unit_handle_t oneshot{};
		adc_cali_handle_t         cali{};
	};
	AdcHandles m_adc{};
	GPIO       m_chargingGpio{};
	bool       m_initialized{};
};
