#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ili9881c.h"
#include "lvgl.h"
#include "gpio/gpio.hpp"
#include "esp_ldo_regulator.h"

/**
 * @brief Display driver for ILI9881C with MIPI-DSI interface
 *
 * Configuration:
 * - Resolution: 720x1280
 * - MIPI DSI: 2 lanes @ 744Mbps
 * - Reset GPIO: GPIO_NUM_5
 * - Color format: RGB888
 */
class Display {
public:
	// LDO configuration for display power
	static constexpr auto LDO_CHAN = 3;
	static constexpr auto LDO_VOLTAGE = 2500;

	// Screen resolution (landscape)
	static constexpr int H_RES = 720;
	static constexpr int V_RES = 1280;

	// MIPI DSI configuration
	static constexpr int MIPI_LANE_BIT_RATE_MBPS = 1200;
	static constexpr int MIPI_LANE_NUM = 2;

	// Reset GPIO
	static constexpr gpio_num_t RESET_GPIO = GPIO_NUM_5;

	// Backlight GPIO
	static constexpr gpio_num_t BACKLIGHT_GPIO = GPIO_NUM_4;

	Display();
	~Display();

	/**
	 * @brief Initialize display with MIPI-DSI and ILI9881C driver
	 * @return true on success, false on failure
	 */
	bool init();

	/**
	 * @brief Get LVGL display handle
	 */
	lv_display_t* getLvglDisplay() { return lvgl_disp; }

	/**
	 * @brief Get ESP LCD panel handle
	 */
	esp_lcd_panel_handle_t getPanel() { return panel; }

private:
	esp_ldo_channel_handle_t ldo_phy{};
	esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
	esp_lcd_panel_io_handle_t panel_io = nullptr;
	esp_lcd_panel_handle_t panel = nullptr;
	lv_display_t* lvgl_disp = nullptr;

	// LVGL buffers
	lv_color_t* buf1 = nullptr;
	lv_color_t* buf2 = nullptr;
};
