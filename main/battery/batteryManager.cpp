#include "battery/batteryManager.hpp"
#include "gui/gui.hpp"
#include "esp_log.h"
#include "gpio/gpio.hpp"

static constexpr char TAG[] = "BatteryManager";

// ════════════════════════════════════════════════════════════════
// 单例
// ════════════════════════════════════════════════════════════════

BatteryManager& BatteryManager::instance()
{
	static BatteryManager s_instance{};
	return s_instance;
}

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

bool BatteryManager::init()
{
	if (m_initialized)
	{
		ESP_LOGW(TAG, "已初始化，跳过");
		return true;
	}

	// ── 1. 创建自有 ADC oneshot 句柄 ──
	adc_oneshot_unit_init_cfg_t adcInit = {
		.unit_id = ADC_UNIT,
	};
	if (adc_oneshot_new_unit(&adcInit, &m_adc.oneshot) != ESP_OK)
	{
		ESP_LOGE(TAG, "创建 ADC 单元失败");
		return false;
	}

	adc_oneshot_chan_cfg_t chanCfg = {
		.atten    = ADC_ATTEN,
		.bitwidth = ADC_BITWIDTH,
	};
	if (adc_oneshot_config_channel(m_adc.oneshot, ADC_CHANNEL, &chanCfg) != ESP_OK)
	{
		ESP_LOGE(TAG, "配置 ADC 通道失败");
		adc_oneshot_del_unit(m_adc.oneshot);
		m_adc.oneshot = nullptr;
		return false;
	}

	// ── 2. 创建 ADC 校准句柄 ──
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
	adc_cali_curve_fitting_config_t caliCfg = {
		.unit_id  = ADC_UNIT,
		.chan     = ADC_CHANNEL,
		.atten    = ADC_ATTEN,
		.bitwidth = ADC_BITWIDTH,
	};
	if (adc_cali_create_scheme_curve_fitting(&caliCfg, &m_adc.cali) != ESP_OK)
	{
		ESP_LOGE(TAG, "创建 ADC 校准（curve fitting）失败");
		adc_oneshot_del_unit(m_adc.oneshot);
		m_adc.oneshot = nullptr;
		return false;
	}
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
	adc_cali_line_fitting_config_t caliCfg = {
		.unit_id  = ADC_UNIT,
		.atten    = ADC_ATTEN,
		.bitwidth = ADC_BITWIDTH,
	};
	if (adc_cali_create_scheme_line_fitting(&caliCfg, &m_adc.cali) != ESP_OK)
	{
		ESP_LOGE(TAG, "创建 ADC 校准（line fitting）失败");
		adc_oneshot_del_unit(m_adc.oneshot);
		m_adc.oneshot = nullptr;
		return false;
	}
#endif

	// ── 3. 初始化充电检测 GPIO ──
	m_chargingGpio = { CHARGING_GPIO, GPIO_MODE_INPUT, GPIO_PULLUP_ONLY };

	// ── 4. 将自有句柄注入 adc_battery_estimation 组件（external 模式） ──
	adc_battery_estimation_t cfg = {};
	cfg.external.adc_handle      = m_adc.oneshot;
	cfg.external.adc_cali_handle = m_adc.cali;
	cfg.adc_channel              = ADC_CHANNEL;
	cfg.upper_resistor           = RESISTOR_UPPER;
	cfg.lower_resistor           = RESISTOR_LOWER;
	cfg.charging_detect_cb       = chargingDetectCb;
	cfg.charging_detect_user_data = this;
	// battery_points / battery_points_count = nullptr/0 → 默认映射

	m_batteryEstHandle = adc_battery_estimation_create(&cfg);
	if (!m_batteryEstHandle)
	{
		ESP_LOGE(TAG, "adc_battery_estimation 初始化失败");
		adc_oneshot_del_unit(m_adc.oneshot);
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
		adc_cali_delete_scheme_curve_fitting(m_adc.cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
		adc_cali_delete_scheme_line_fitting(m_adc.cali);
#endif
		m_adc = {};
		return false;
	}

	m_initialized = true;
	ESP_LOGI(TAG, "初始化完成（通道 %d, 分压 %.1f/%.1f kΩ）",
			 ADC_CHANNEL, RESISTOR_UPPER, RESISTOR_LOWER);
	return true;
}

void BatteryManager::deinit()
{
	// 先销毁组件（external 模式不会删除我们的句柄）
	if (m_batteryEstHandle)
	{
		adc_battery_estimation_destroy(
			(adc_battery_estimation_handle_t)m_batteryEstHandle);
		m_batteryEstHandle = nullptr;
	}

	// 再销毁自有 ADC 句柄
	if (m_adc.cali)
	{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
		adc_cali_delete_scheme_curve_fitting(m_adc.cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
		adc_cali_delete_scheme_line_fitting(m_adc.cali);
#endif
		m_adc.cali = nullptr;
	}
	if (m_adc.oneshot)
	{
		adc_oneshot_del_unit(m_adc.oneshot);
		m_adc.oneshot = nullptr;
	}

	m_initialized = false;
	ESP_LOGI(TAG, "已释放");
}

// ════════════════════════════════════════════════════════════════
// 数据读取
// ════════════════════════════════════════════════════════════════

int BatteryManager::getPercent()
{
	if (!m_batteryEstHandle)
	{
		ESP_LOGW(TAG, "未初始化，返回默认值 50%%");
		return 50;
	}

	float capacity = 0;
	esp_err_t ret = adc_battery_estimation_get_capacity(
		(adc_battery_estimation_handle_t)m_batteryEstHandle, &capacity);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "读取电量失败: %s", esp_err_to_name(ret));
		return -1;
	}

	int pct = (int)(capacity + 0.5f);
	if (pct < 0)   pct = 0;
	if (pct > 100) pct = 100;
	return pct;
}

int BatteryManager::getVoltageMv()
{
	if (!m_adc.oneshot)
	{
		ESP_LOGW(TAG, "ADC 未初始化，返回 0");
		return 0;
	}

	// 读取 ADC 原始值
	int raw = 0;
	if (adc_oneshot_read(m_adc.oneshot, ADC_CHANNEL, &raw) != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC 读取失败");
		return -1;
	}

	// 校准为 ADC pin 电压 (mV)
	int pinMv = 0;
	if (adc_cali_raw_to_voltage(m_adc.cali, raw, &pinMv) != ESP_OK)
	{
		ESP_LOGE(TAG, "ADC 校准失败");
		return -1;
	}

	// 分压还原：Vbat = Vpin * (R_upper + R_lower) / R_lower
	float ratio = (RESISTOR_UPPER + RESISTOR_LOWER) / RESISTOR_LOWER;
	int batMv = (int)((float)pinMv * ratio + 0.5f);

	return batMv;
}

// ════════════════════════════════════════════════════════════════
// 充电状态
// ════════════════════════════════════════════════════════════════

bool BatteryManager::isCharging()
{
	if (!m_batteryEstHandle) return false;

	bool charging = false;
	adc_battery_estimation_get_charging_state(
		(adc_battery_estimation_handle_t)m_batteryEstHandle, &charging);
	return charging;
}

bool BatteryManager::chargingDetectCb(void* userData)
{
	(void)userData;
	// 低电平 = 充电中（GPIO_PULLUP_ONLY，不插入时被拉高）
	return !(bool)(GPIO{ CHARGING_GPIO });
}

// ════════════════════════════════════════════════════════════════
// 静态辅助方法
// ════════════════════════════════════════════════════════════════

const char* BatteryManager::getIcon(int percent)
{
	if (percent >= 80)       return LV_SYMBOL_BATTERY_FULL;
	else if (percent >= 60)  return LV_SYMBOL_BATTERY_3;
	else if (percent >= 40)  return LV_SYMBOL_BATTERY_2;
	else if (percent >= 20)  return LV_SYMBOL_BATTERY_1;
	else                     return LV_SYMBOL_BATTERY_EMPTY;
}

lv_color_t BatteryManager::getColor(int percent)
{
	if (percent >= 61)       return GUI::Color::SUCCESS;  // 绿
	else if (percent >= 21)  return GUI::Color::WARNING;  // 黄
	else                     return GUI::Color::DANGER;   // 红
}
