#include "ili9881c.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_err.h>
#include "initCommand.inl"
#include "esp_lv_adapter.h"

static const char* TAG = "ILI9881c";

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
	ESP_LOGI(TAG, "ILI9881C panel initialized");
	GPIO{ BACKLIGHT_GPIO, GPIO::Mode::GPIO_MODE_OUTPUT } = 1;

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
