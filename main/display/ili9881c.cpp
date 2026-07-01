#include "ili9881c.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_err.h>
#include "initCommand.inl"
#include "esp_lv_adapter.h"
#include "driver/ledc.h"
#include <cstdint>

static const char* TAG = "ILI9881c";

// ── 背光驱动电压窗口 ──
// PWM 10-bit → 3.3V: duty=1023 → 3.3V
// 背光驱动有效调压范围 0.7V~1.4V → duty 217~434
// 放高数值以获取更一致的效果
static constexpr uint16_t BACKLIGHT_DUTY_MIN = 230;
static constexpr uint16_t BACKLIGHT_DUTY_MAX = 450;

// ── 伽马校正查找表 ──
// gamma 2.2: duty = round(1023 * (percent/100)^2.2)
// 11 节点 + 整数线性插值，补偿人眼感知非线性
static constexpr uint16_t kGammaLUT[11] = {
	0,    // 0%
	6,    // 10%
	28,   // 20%
	68,   // 30%
	130,  // 40%
	220,  // 50%
	338,  // 60%
	486,  // 70%
	663,  // 80%
	870,  // 90%
	1023, // 100%
};

static uint16_t dutyFromPercent(int percent)
{
	if (percent <= 0) return 0;
	if (percent >= 100) return 1023;
	int idx = percent / 10;
	int rem = percent % 10;
	return kGammaLUT[idx] + (int(kGammaLUT[idx + 1] - kGammaLUT[idx]) * rem) / 10;
}

bool ILI9881c::brightnessInit()
{
	ledc_timer_config_t tm = {
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.duty_resolution = LEDC_TIMER_10_BIT,
		.timer_num = LEDC_TIMER_1,
		.freq_hz = 5000,
		.clk_cfg = LEDC_AUTO_CLK,
	};
	ledc_channel_config_t ch = {
		.gpio_num = BACKLIGHT_GPIO,
		.speed_mode = LEDC_LOW_SPEED_MODE,
		.channel = LEDC_CHANNEL_0,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = LEDC_TIMER_1,
		.duty = BACKLIGHT_DUTY_MAX,
		.hpoint = 0,
	};
	ESP_ERROR_CHECK(ledc_timer_config(&tm));
	ESP_ERROR_CHECK(ledc_channel_config(&ch));
	m_brightness = 100;
	ESP_LOGI(TAG, "背光 PWM 初始化完成 (GPIO %d, 10-bit, 5kHz)", BACKLIGHT_GPIO);
	return true;
}

void ILI9881c::brightnessSet(int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;

	uint32_t duty;
	if (percent <= 0) {
		duty = 0;  // 0% → 关背光
	} else {
		// 第 1 层: 伽马校正 (0..1023)
		uint32_t gammaDuty = dutyFromPercent(percent);
		// 第 2 层: 窗口映射到背光驱动有效电压 (217..434)
		duty = BACKLIGHT_DUTY_MIN + (gammaDuty * (BACKLIGHT_DUTY_MAX - BACKLIGHT_DUTY_MIN)) / 1023;
	}

	ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
	ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
	m_brightness = percent;

	ESP_LOGI(TAG, "Backlight duty set to %d%%, duty = %u", percent, duty);
}

int ILI9881c::brightnessGet()
{
	return m_brightness;
}

ILI9881c::ILI9881c() = default;

ILI9881c::~ILI9881c()
{
	deinit();
}

ILI9881c& ILI9881c::getInstance()
{
	static ILI9881c inst;
	return inst;
}

bool ILI9881c::init(int h_res, int v_res, uint8_t num_fbs)
{
	esp_err_t ret;

	// LDO 供电
	esp_ldo_channel_config_t ldo_cfg{};
	ldo_cfg.chan_id = LDO_CHAN;
	ldo_cfg.voltage_mv = LDO_VOLTAGE;
	ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_phy));
	ESP_LOGI(TAG, "PHY power on");

	// MIPI DSI bus
	esp_lcd_dsi_bus_config_t bus_config =
	{};
	bus_config.bus_id = 0;
	bus_config.num_data_lanes = MIPI_LANE_NUM;
	bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
	bus_config.lane_bit_rate_mbps = MIPI_LANE_BIT_RATE_MBPS;

	ret = esp_lcd_new_dsi_bus(&bus_config, &dsi_bus);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to create DSI bus: %s", esp_err_to_name(ret));
		deinit();
		return false;
	}
	ESP_LOGI(TAG, "MIPI DSI bus created: %d lanes @ %dMbps", MIPI_LANE_NUM, MIPI_LANE_BIT_RATE_MBPS);

	// DBI panel IO
	esp_lcd_dbi_io_config_t io_config =
	{};
	io_config.virtual_channel = 0;
	io_config.lcd_cmd_bits = 8;
	io_config.lcd_param_bits = 8;

	ret = esp_lcd_new_panel_io_dbi(dsi_bus, &io_config, &panel_io);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
		deinit();
		return false;
	}

	// DPI panel config
	esp_lcd_dpi_panel_config_t dpi_config =
	{};
	dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
	dpi_config.dpi_clock_freq_mhz = 62;
	dpi_config.virtual_channel = 0;

	static_assert(LV_COLOR_DEPTH == 24 || LV_COLOR_DEPTH == 16, "Color depth must be 24 or 16");
	if constexpr (LV_COLOR_DEPTH == 24)
		dpi_config.in_color_format = lcd_color_format_t::LCD_COLOR_FMT_RGB888;
	else if constexpr (LV_COLOR_DEPTH == 16)
		dpi_config.in_color_format = lcd_color_format_t::LCD_COLOR_FMT_RGB565;

	dpi_config.num_fbs = num_fbs;
	dpi_config.video_timing.h_size = h_res;
	dpi_config.video_timing.v_size = v_res;
	dpi_config.video_timing.hsync_back_porch = 30;
	dpi_config.video_timing.hsync_pulse_width = 4;
	dpi_config.video_timing.hsync_front_porch = 30;
	dpi_config.video_timing.vsync_back_porch = 14;
	dpi_config.video_timing.vsync_pulse_width = 2;
	dpi_config.video_timing.vsync_front_porch = 20;
	dpi_config.flags.use_dma2d = true;

	ili9881c_vendor_config_t vendor_config =
	{};
	vendor_config.init_cmds = vendor_init_cmds;
	vendor_config.init_cmds_size = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]);
	vendor_config.mipi_config.dsi_bus = dsi_bus;
	vendor_config.mipi_config.dpi_config = &dpi_config;
	vendor_config.mipi_config.lane_num = MIPI_LANE_NUM;

	esp_lcd_panel_dev_config_t panel_config =
	{};
	panel_config.reset_gpio_num = RESET_GPIO;
	panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
	panel_config.bits_per_pixel = LV_COLOR_DEPTH;
	panel_config.vendor_config = &vendor_config;

	ret = esp_lcd_new_panel_ili9881c(panel_io, &panel_config, &panel);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to create ILI9881C panel: %s", esp_err_to_name(ret));
		deinit();
		return false;
	}

	// Reset & init panel
	ret = esp_lcd_panel_reset(panel);
	if (ret == ESP_OK)
	{
		vTaskDelay(pdMS_TO_TICKS(10));
		ret = esp_lcd_panel_init(panel);
	}
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to init panel: %s", esp_err_to_name(ret));
		deinit();
		return false;
	}

	// Turn on display + backlight
	esp_lcd_panel_disp_on_off(panel, true);
	getInstance().brightnessInit();
	ESP_LOGI(TAG, "ILI9881C panel initialized");

	return true;
}

void ILI9881c::deinit()
{
	if (panel)
	{
		esp_lcd_panel_del(panel);
		panel = nullptr;
	}
	if (panel_io)
	{
		esp_lcd_panel_io_del(panel_io);
		panel_io = nullptr;
	}
	if (dsi_bus)
	{
		esp_lcd_del_dsi_bus(dsi_bus);
		dsi_bus = nullptr;
	}
	if (ldo_phy)
	{
		esp_ldo_release_channel(ldo_phy);
		ldo_phy = nullptr;
	}
}
