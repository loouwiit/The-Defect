#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_ili9881c.h"

#include "gpio/gpio.hpp"

#include "ili9881c_init_cmds.h"

static const char* TAG = "ili9881c_example";

// ================ 请根据实际硬件修改以下参数 ================
// 背光引脚（-1 表示未使用）
#define PIN_BL                  (GPIO_NUM_47)
#define BL_ON_LEVEL             1

// 复位引脚（-1表示不使用硬件复位，或自己接）
#define PIN_RST                 (GPIO_NUM_48)
// MIPI DSI PHY 供电 LDO 通道
#define LDO_CHAN                3
#define LDO_VOLTAGE_MV          2500
// MIPI 数据通道数
#define MIPI_LANE_NUM           2
// ============================================================

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
static esp_lcd_panel_io_handle_t mipi_dbi_io = NULL;
static esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
static SemaphoreHandle_t refresh_finish = NULL;

static IRAM_ATTR bool on_color_trans_done(esp_lcd_panel_handle_t panel,
	esp_lcd_dpi_panel_event_data_t* edata,
	void* user_ctx)
{
	SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
	BaseType_t need_yield = pdFALSE;
	xSemaphoreGiveFromISR(sem, &need_yield);
	return need_yield == pdTRUE;
}

static void lcd_init(void)
{
	// 1. 开启背光
#if PIN_BL >= 0
	GPIO{ PIN_BL, GPIO::Mode::GPIO_MODE_OUTPUT } = BL_ON_LEVEL;
#endif

	// 2. MIPI DSI PHY 供电
	esp_ldo_channel_config_t ldo_config = {};
	ldo_config.chan_id = LDO_CHAN;
	ldo_config.voltage_mv = LDO_VOLTAGE_MV;
	ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_config, &ldo_mipi_phy));
	ESP_LOGI(TAG, "MIPI DSI PHY powered on");

	// 3. 创建 MIPI DSI 总线
	esp_lcd_dsi_bus_config_t bus_config = {};
	bus_config.bus_id = 0;
	bus_config.num_data_lanes = MIPI_LANE_NUM;
	bus_config.phy_clk_src = mipi_dsi_phy_pllref_clock_source_t::MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT_LEGACY;
	bus_config.lane_bit_rate_mbps = 500;
	ESP_ERROR_CHECK(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus));

	// 4. 创建 DBI 命令接口（用于初始化命令）
	esp_lcd_dbi_io_config_t dbi_config = {};
	dbi_config.virtual_channel = 0;
	dbi_config.lcd_cmd_bits = 8;
	dbi_config.lcd_param_bits = 8;
	ESP_ERROR_CHECK(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &mipi_dbi_io));

	// 5. 配置 DPI 时序（根据你的 ILI9881C 屏幕规格调整）
	esp_lcd_dpi_panel_config_t dpi_config = {};
	dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
	dpi_config.dpi_clock_freq_mhz = 80;
	dpi_config.virtual_channel = 0;
	dpi_config.pixel_format = lcd_color_rgb_pixel_format_t::LCD_COLOR_PIXEL_FORMAT_RGB888;
	dpi_config.in_color_format = lcd_color_format_t::LCD_COLOR_FMT_RGB888;
	dpi_config.out_color_format = lcd_color_format_t::LCD_COLOR_FMT_RGB888;
	dpi_config.num_fbs = 1;
	// ========== 修改 dpi_config 视频时序为 720×1280 ==========
	dpi_config.video_timing.h_size = 720;
	dpi_config.video_timing.v_size = 1280;
	dpi_config.video_timing.hsync_back_porch = 30;   // 从 dts 取
	dpi_config.video_timing.hsync_pulse_width = 4;
	dpi_config.video_timing.hsync_front_porch = 30;
	dpi_config.video_timing.vsync_back_porch = 14;
	dpi_config.video_timing.vsync_pulse_width = 2;
	dpi_config.video_timing.vsync_front_porch = 20;
	// clock-frequency = 62 MHz，但 dpi_clock_freq_mhz 需匹配 lane_rate
	// 先设 30 MHz 试试
	dpi_config.dpi_clock_freq_mhz = 30;

	dpi_config.flags.use_dma2d = true;

	// 6. vendor_config 必须传入，否则 esp_lcd_new_panel_ili9881c 返回 ESP_ERR_INVALID_ARG
	ili9881c_vendor_config_t vendor_config = {};
	// ========== 修改 vendor_config 使用自定义序列 ==========
	vendor_config.init_cmds = vendor_init_cmds;
	vendor_config.init_cmds_size = sizeof(vendor_init_cmds) / sizeof(vendor_init_cmds[0]);
	vendor_config.mipi_config.dsi_bus = mipi_dsi_bus;
	vendor_config.mipi_config.dpi_config = &dpi_config;
	vendor_config.mipi_config.lane_num = MIPI_LANE_NUM;

	// 7. 创建 ILI9881C 面板
	// 注意：esp_lcd_new_panel_ili9881c 会在内部自动调用 esp_lcd_new_panel_dpi，
	// 返回的 panel_handle 直接就是 DPI 面板句柄，不要额外再创建一次！
	esp_lcd_panel_dev_config_t panel_config = {};
	panel_config.reset_gpio_num = PIN_RST;
	panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
	panel_config.bits_per_pixel = 24;
	panel_config.vendor_config = &vendor_config;
	ESP_ERROR_CHECK(esp_lcd_new_panel_ili9881c(mipi_dbi_io, &panel_config, &panel_handle));

	// 8. 初始化
	ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
	ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

	// 9. 注册刷新完成回调
	refresh_finish = xSemaphoreCreateBinary();
	esp_lcd_dpi_panel_event_callbacks_t cbs = {};
	cbs.on_color_trans_done = on_color_trans_done;
	ESP_ERROR_CHECK(esp_lcd_dpi_panel_register_event_callbacks(panel_handle, &cbs, refresh_finish));

	ESP_LOGI(TAG, "LCD initialized");
}

static void lcd_deinit(void)
{
	if (panel_handle) {
		esp_lcd_panel_del(panel_handle);
		panel_handle = NULL;
	}
	if (mipi_dbi_io) {
		esp_lcd_panel_io_del(mipi_dbi_io);
		mipi_dbi_io = NULL;
	}
	if (mipi_dsi_bus) {
		esp_lcd_del_dsi_bus(mipi_dsi_bus);
		mipi_dsi_bus = NULL;
	}
	if (ldo_mipi_phy) {
		esp_ldo_release_channel(ldo_mipi_phy);
		ldo_mipi_phy = NULL;
	}
	if (refresh_finish) {
		vSemaphoreDelete(refresh_finish);
		refresh_finish = NULL;
	}
	ESP_LOGI(TAG, "LCD deinitialized");
}

static void fill_screen(uint8_t r, uint8_t g, uint8_t b)
{
	int bpp = 3;
	size_t buf_size = 720 * 1280 * bpp;
	uint8_t* buf = (uint8_t*)heap_caps_malloc(buf_size, MALLOC_CAP_DMA);
	if (!buf) {
		ESP_LOGE(TAG, "malloc failed");
		return;
	}
	for (int i = 0; i < 800 * 1280; i++) {
		buf[i * bpp] = r;
		buf[i * bpp + 1] = g;
		buf[i * bpp + 2] = b;
	}
	ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, 800, 1280, buf));
	xSemaphoreTake(refresh_finish, portMAX_DELAY);
	free(buf);
}

extern "C" void app_main(void)
{
	lcd_init();

	while (true)
	{
		ESP_LOGI(TAG, "Show red");
		fill_screen(255, 0, 0);
		vTaskDelay(pdMS_TO_TICKS(2000));

		ESP_LOGI(TAG, "Show green");
		fill_screen(0, 255, 0);
		vTaskDelay(pdMS_TO_TICKS(2000));

		ESP_LOGI(TAG, "Show blue");
		fill_screen(0, 0, 255);
		vTaskDelay(pdMS_TO_TICKS(2000));
	}

	lcd_deinit();
}
