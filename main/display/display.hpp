#pragma once

#include <cstdint>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_ili9881c.h"
#include "gpio/gpio.hpp"
#include "esp_ldo_regulator.h"
#include "esp_lv_adapter.h"

/**
 * @brief Display driver for ILI9881C with MIPI-DSI interface
 *
 * Handles everything display-related:
 * - LCD hardware initialization (MIPI DSI bus, panel IO, panel, backlight)
 * - LVGL adapter integration (adapter init, display registration, task start)
 *
 * Usage:
 * @code
 *   Display display;
 *   if (!display.init()) return;
 *   // optional: register touch input here
 *   if (!display.start()) return;
 *   // use LVGL via display.lock() / display.unlock()
 * @endcode
 *
 * Configuration:
 * - Resolution: 720x1280
 * - MIPI DSI: 2 lanes @ 1200Mbps
 * - Color format: RGB565 / RGB888 (LV_COLOR_DEPTH=16/24)
 */
class Display {
public:
	// LDO configuration for display power
	static constexpr auto LDO_CHAN = 3;
	static constexpr auto LDO_VOLTAGE = 2500;

	// Screen resolution
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
	 * @brief Initialize display hardware + LVGL adapter
	 *
	 * Performs LCD hardware init, then initializes LVGL adapter and
	 * registers the display. Does NOT start the LVGL worker task
	 * (call start() after optionally registering input devices).
	 *
	 * @param rotation Display rotation (0/90/180/270)
	 * @return true on success
	 */
	bool init(esp_lv_adapter_rotation_t rotation = ESP_LV_ADAPTER_ROTATE_0);

	/**
	 * @brief Start the LVGL worker task
	 *
	 * Call this after init() and after optionally registering input
	 * devices (touch, buttons, etc.).
	 * @return true on success
	 */
	bool start();

	/** @brief Get LVGL display handle */
	lv_display_t* getLvglDisplay() const { return lv_disp; }

	/** @brief Get ESP LCD panel handle */
	esp_lcd_panel_handle_t getPanel() const { return panel; }

	/** @brief Get ESP LCD panel IO handle */
	esp_lcd_panel_io_handle_t getPanelIo() const { return panel_io; }

	/**
	 * @brief Lock LVGL for thread-safe access
	 * @param timeout_ms Timeout (-1 = infinite)
	 * @return true if lock acquired
	 */
	bool lock(int32_t timeout_ms = -1) const {
		return esp_lv_adapter_lock(timeout_ms) == ESP_OK;
	}

	/** @brief Unlock LVGL */
	void unlock() const { esp_lv_adapter_unlock(); }

private:
	esp_ldo_channel_handle_t ldo_phy{};
	esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
	esp_lcd_panel_io_handle_t panel_io = nullptr;
	esp_lcd_panel_handle_t panel = nullptr;
	lv_display_t* lv_disp = nullptr;
};
