#include <stdio.h>
#include <thread>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"          // ← 新增
#include "esp_ldo_regulator.h"
#include "gpio/gpio.hpp"

#include "task/task.hpp"

#include "ili9881c_init_cmds.h"

static const char* TAG = "minimal_mipi_test";

#define PIN_BL      (GPIO_NUM_4)
#define BL_ON_LEVEL 1
#define LDO_CHAN    3
#define LDO_VOLTAGE 2500
#define LANE_NUM    2

#define PIN_RST     (GPIO_NUM_5)

static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t dbi_io = NULL;
static esp_lcd_panel_handle_t   dpi_panel = NULL;
static esp_ldo_channel_handle_t ldo_phy = NULL;

extern "C" void app_main(void)
{
	ESP_LOGI(TAG, "Starting minimal MIPI DSI test with built-in pattern");

	new std::thread{ [](){while (true) { ESP_LOGI(TAG, "heartbeat"); vTaskDelay(1000); }} };

	// 拉低复位
	GPIO{ PIN_RST, GPIO::Mode::GPIO_MODE_OUTPUT } = 0;

	// LDO 供电
	esp_ldo_channel_config_t ldo_cfg = {};
	ldo_cfg.chan_id = LDO_CHAN;
	ldo_cfg.voltage_mv = LDO_VOLTAGE;
	ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_cfg, &ldo_phy));
	ESP_LOGI(TAG, "PHY power on");

	// 开启背光
	GPIO{ PIN_BL, GPIO::Mode::GPIO_MODE_OUTPUT } = BL_ON_LEVEL;

	// 创建 MIPI DSI 总线 (2-lane)
	esp_lcd_dsi_bus_config_t bus_cfg = {};
	bus_cfg.bus_id = 0;
	bus_cfg.num_data_lanes = LANE_NUM;
	bus_cfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
	bus_cfg.lane_bit_rate_mbps = 744;
	ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_cfg, &mipi_dsi_bus));
	ESP_LOGI(TAG, "DSI bus created");

	// ---------- 新增：创建 DBI 接口并发送最小初始化命令 ----------
	esp_lcd_dbi_io_config_t dbi_config = {};
	dbi_config.virtual_channel = 0;
	dbi_config.lcd_cmd_bits = 8;
	dbi_config.lcd_param_bits = 8;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &dbi_io));
	ESP_LOGI(TAG, "DBI IO created");

	vTaskDelay(200);

	// 复位
	GPIO{ PIN_RST } = 1;
	vTaskDelay(1);
	GPIO{ PIN_RST } = 0;
	vTaskDelay(1);
	GPIO{ PIN_RST } = 1;
	vTaskDelay(200);

	ESP_LOGI(TAG, "Reciving panel ID using DBI...");
	int commands[4] = { 0x04 };
	esp_lcd_panel_io_rx_param(dbi_io, 1, commands, 1);
	ESP_LOGI(TAG, "Recieved panel ID: %02x", commands[0]);

	ESP_LOGI(TAG, "Sending complete init sequence...");
	for (int i = 0; i < sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]); i++) {
		ESP_LOGI(TAG, "Sending command %x", vendor_init_cmds[i].cmd);
		ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(dbi_io,
			vendor_init_cmds[i].cmd,
			vendor_init_cmds[i].data,
			vendor_init_cmds[i].data_bytes));
		if (vendor_init_cmds[i].delay_ms > 0) {
			vTaskDelay(pdMS_TO_TICKS(vendor_init_cmds[i].delay_ms));
		}
	}
	ESP_LOGI(TAG, "Complete init sequence sent");
	vTaskDelay(pdMS_TO_TICKS(50));

	// 注意：这里我们不删除 dbi_io，面板会继续使用（但 DPI 模式会接管视频传输）
	// 后续会同时使用 DBI 和 DPI，但 DPI 会覆盖 DBI 视频模式，这没问题。
	// ----------------------------------------------------------

	// 直接创建 DPI 面板
	esp_lcd_dpi_panel_config_t dpi_cfg = {};
	dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
	dpi_cfg.dpi_clock_freq_mhz = 62;
	dpi_cfg.virtual_channel = 0;
	dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB888;
	dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB888;
	dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB888;
	dpi_cfg.num_fbs = 1;
	// 使用 720x1280 时序（可根据屏幕修改）
	dpi_cfg.video_timing.h_size = 720;
	dpi_cfg.video_timing.v_size = 1280;
	// 来自 dts：hback-porch=30, hsync-len=4, hfront-porch=30
	dpi_cfg.video_timing.hsync_back_porch = 30;
	dpi_cfg.video_timing.hsync_pulse_width = 4;
	dpi_cfg.video_timing.hsync_front_porch = 30;
	// 来自 dts：vback-porch=14, vsync-len=2, vfront-porch=20
	dpi_cfg.video_timing.vsync_back_porch = 14;
	dpi_cfg.video_timing.vsync_pulse_width = 2;
	dpi_cfg.video_timing.vsync_front_porch = 20;
	dpi_cfg.flags.use_dma2d = true;

	ESP_ERROR_CHECK(esp_lcd_new_panel_dpi(mipi_dsi_bus, &dpi_cfg, &dpi_panel));
	ESP_LOGI(TAG, "DPI panel created");

	ESP_ERROR_CHECK(esp_lcd_panel_init(dpi_panel));
	ESP_LOGI(TAG, "DPI panel initialized");

	// 设置硬件测试图案
	Task::init(1);
	Task::addTask([](void*)->TickType_t {ESP_LOGI("heartbeat", "heartbeat"); return 100; }, "heartbeat", nullptr, 0, Task::Affinity::None);

	while (true)
	{
		ESP_LOGI(TAG, "Setting pattern: vertical color bars");
		ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(dpi_panel, mipi_dsi_pattern_type_t::MIPI_DSI_PATTERN_BAR_VERTICAL));
		vTaskDelay(pdMS_TO_TICKS(1000));

		ESP_LOGI(TAG, "Setting pattern: horizontal color bars");
		ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(dpi_panel, mipi_dsi_pattern_type_t::MIPI_DSI_PATTERN_BAR_HORIZONTAL));
		vTaskDelay(pdMS_TO_TICKS(1000));

		ESP_LOGI(TAG, "Setting pattern: vertical BER");
		ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(dpi_panel, mipi_dsi_pattern_type_t::MIPI_DSI_PATTERN_BER_VERTICAL));
		vTaskDelay(pdMS_TO_TICKS(1000));
	}

	// 取消测试图案
	ESP_LOGI(TAG, "Clearing pattern");
	ESP_ERROR_CHECK(esp_lcd_dpi_panel_set_pattern(dpi_panel, MIPI_DSI_PATTERN_NONE));

	// 清理
	if (dbi_io) {
		esp_lcd_panel_io_del(dbi_io);
	}
	esp_lcd_panel_del(dpi_panel);
	esp_lcd_del_dsi_bus(mipi_dsi_bus);
	esp_ldo_release_channel(ldo_phy);

	ESP_LOGI(TAG, "Test finished");
}
