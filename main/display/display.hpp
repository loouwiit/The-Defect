#pragma once

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ili9881c.h"
#include "lvgl.h"
#include "driver/gpio.h"

/**
 * @brief Display driver for ILI9881C with MIPI-DSI interface
 *
 * Configuration:
 * - Resolution: 800x1280
 * - MIPI DSI: 2 lanes @ 600Mbps
 * - Reset GPIO: GPIO_NUM_15
 * - Color format: RGB565
 */
class Display {
public:
	// Screen resolution
	static constexpr int H_RES = 800;
	static constexpr int V_RES = 1280;

	// MIPI DSI configuration
	static constexpr int MIPI_LANE_BIT_RATE_MBPS = 600;
	static constexpr int MIPI_LANE_NUM = 2;

	// Reset GPIO
	static constexpr gpio_num_t RESET_GPIO = GPIO_NUM_15;

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
	esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
	esp_lcd_panel_io_handle_t panel_io = nullptr;
	esp_lcd_panel_handle_t panel = nullptr;
	lv_display_t* lvgl_disp = nullptr;

	// LVGL buffers
	lv_color_t* buf1 = nullptr;
	lv_color_t* buf2 = nullptr;
};
