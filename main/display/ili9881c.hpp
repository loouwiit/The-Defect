#pragma once

#include <cstdint>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ili9881c.h"
#include "esp_ldo_regulator.h"
#include "gpio/gpio.hpp"

class ILI9881c
{
public:
	static ILI9881c& getInstance();

	ILI9881c();
	~ILI9881c();

	// Initialize the display hardware (DSI bus, panel IO, panel).
	// h_res/v_res are the panel resolution; num_fbs is the number of frame buffers
	// calculated by the LVGL adapter.
	bool init(int h_res, int v_res, uint8_t num_fbs);
	void deinit();

	esp_lcd_panel_handle_t getPanel() const
	{
		return panel;
	}
	esp_lcd_panel_io_handle_t getPanelIo() const
	{
		return panel_io;
	}

	// ── 背光亮度控制 ──
	bool initBrightness();
	void setBrightness(int percent);
	int getBrightness();
	void saveBrightnessToNvs();
	void loadBrightnessFromNvs();

private:
	esp_ldo_channel_handle_t ldo_phy{};
	int m_brightness{ 100 };
	esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
	esp_lcd_panel_io_handle_t panel_io = nullptr;
	esp_lcd_panel_handle_t panel = nullptr;

	// Hardware configuration
	static constexpr auto LDO_CHAN = 3;
	static constexpr auto LDO_VOLTAGE = 2500;
	static constexpr int MIPI_LANE_BIT_RATE_MBPS = 900;
	static constexpr int MIPI_LANE_NUM = 2;
	static constexpr gpio_num_t RESET_GPIO = GPIO_NUM_27;
	static constexpr gpio_num_t BACKLIGHT_GPIO = GPIO_NUM_26;
};
