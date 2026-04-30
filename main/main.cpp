#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#include "gpio/gpio.hpp"

static const char* TAG = "minimal_mipi_test";

#define PIN_BL      (GPIO_NUM_47)
#define BL_ON_LEVEL 1
#define LDO_CHAN    3
#define LDO_VOLTAGE 2500
#define LANE_NUM    2

static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_handle_t   dpi_panel = NULL;
static esp_ldo_channel_handle_t ldo_phy = NULL;

extern "C" void app_main(void)
{
	ESP_LOGI(TAG, "Starting minimal MIPI DSI test with built-in pattern");

	// 拉高reset
	GPIO{ GPIO_NUM_48, GPIO::Mode::GPIO_MODE_OUTPUT } = 1;

	// 开启背光
	GPIO{ PIN_BL, GPIO::Mode::GPIO_MODE_OUTPUT } = BL_ON_LEVEL;

	// LDO 供电
	esp_ldo_channel_config_t ldo_cfg = {};
	ldo_cfg.chan_id = LDO_CHAN;
	ldo_cfg.voltage_mv = LDO_VOLTAGE;
	ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_phy));
	ESP_LOGI(TAG, "PHY power on");

	// 创建 MIPI DSI 总线 (2-lane, 500 Mbps)
	esp_lcd_dsi_bus_config_t bus_cfg = {};
	bus_cfg.bus_id = 0;
	bus_cfg.num_data_lanes = LANE_NUM;
	bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
	bus_cfg.lane_bit_rate_mbps = 500;
	ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &mipi_dsi_bus));
	ESP_LOGI(TAG, "DSI bus created (500 Mbps)");

	// 直接创建 DPI 面板（不需要 DBI 接口，不涉及任何 LCD 寄存器通信）
	esp_lcd_dpi_panel_config_t dpi_cfg = {};
	dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
	dpi_cfg.dpi_clock_freq_mhz = 80;
	dpi_cfg.virtual_channel = 0;
	dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
	dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB888;
	dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB888;
	dpi_cfg.num_fbs = 1;
	// 使用 720x1280 时序（可根据屏幕修改）
	dpi_cfg.video_timing.h_size = 720;
	dpi_cfg.video_timing.v_size = 1280;
	dpi_cfg.video_timing.hsync_back_porch = 140;
	dpi_cfg.video_timing.hsync_pulse_width = 40;
	dpi_cfg.video_timing.hsync_front_porch = 40;
	dpi_cfg.video_timing.vsync_back_porch = 16;
	dpi_cfg.video_timing.vsync_pulse_width = 4;
	dpi_cfg.video_timing.vsync_front_porch = 16;
	dpi_cfg.flags.use_dma2d = true;

	ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_cfg, &dpi_panel));
	ESP_LOGI(TAG, "DPI panel created");

	ESP_ERROR_CHECK(esp_lcd_panel_init(dpi_panel));
	ESP_LOGI(TAG, "DPI panel initialized");

	// 设置硬件测试图案：垂直彩条（由 MIPI DSI 第部硬件生成，不依赖 LCD 初始化）
	ESP_LOGI(TAG, "Setting pattern: vertical color bars");
	ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(dpi_panel, MIPI_DSI_PATTERN_BAR_VERTICAL));

	// 保持图案显示 30 秒以便观察
	vTaskDelay(pdMS_TO_TICKS(30000));

	// 取消测试图案
	ESP_LOGI(TAG, "Clearing pattern");
	ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(dpi_panel, MIPI_DSI_PATTERN_NONE));

	// 清理
	esp_lcd_panel_del(dpi_panel);
	esp_lcd_del_dsi_bus(mipi_dsi_bus);
	esp_ldo_release_channel(ldo_phy);

	ESP_LOGI(TAG, "Test finished");
}
