#include "display.hpp"
#include "esp_log.h"
#include "esp_dma_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "Display";

Display::Display() = default;

Display::~Display()
{
	if (buf1) heap_caps_free(buf1);
	if (buf2) heap_caps_free(buf2);
	if (panel) esp_lcd_panel_del(panel);
	if (panel_io) esp_lcd_panel_io_del(panel_io);
	if (dsi_bus) esp_lcd_del_dsi_bus(dsi_bus);
}

bool Display::init()
{
	esp_err_t ret;

	// 1. Create MIPI DSI bus
	esp_lcd_dsi_bus_config_t bus_config = {};
	bus_config.bus_id = 0;
	bus_config.num_data_lanes = MIPI_LANE_NUM;
	bus_config.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
	bus_config.lane_bit_rate_mbps = MIPI_LANE_BIT_RATE_MBPS;

	ret = esp_lcd_new_dsi_bus(&bus_config, &dsi_bus);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create DSI bus: %s", esp_err_to_name(ret));
		return false;
	}
	ESP_LOGI(TAG, "MIPI DSI bus created: %d lanes @ %dMbps", MIPI_LANE_NUM, MIPI_LANE_BIT_RATE_MBPS);

	// 2. Create DBI panel IO
	esp_lcd_dbi_io_config_t io_config = {};
	io_config.virtual_channel = 0;
	io_config.lcd_cmd_bits = 8;
	io_config.lcd_param_bits = 8;

	ret = esp_lcd_new_panel_io_dbi(dsi_bus, &io_config, &panel_io);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
		return false;
	}

	// 3. Configure DPI panel
	esp_lcd_dpi_panel_config_t dpi_config = {};
	dpi_config.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
	dpi_config.dpi_clock_freq_mhz = 80;
	dpi_config.virtual_channel = 0;
	dpi_config.in_color_format = lcd_color_format_t::LCD_COLOR_FMT_RGB565;
	dpi_config.num_fbs = 1;
	dpi_config.video_timing.h_size = H_RES;
	dpi_config.video_timing.v_size = V_RES;
	dpi_config.video_timing.hsync_back_porch = 140;
	dpi_config.video_timing.hsync_pulse_width = 40;
	dpi_config.video_timing.hsync_front_porch = 40;
	dpi_config.video_timing.vsync_back_porch = 16;
	dpi_config.video_timing.vsync_pulse_width = 4;
	dpi_config.video_timing.vsync_front_porch = 16;

	ili9881c_vendor_config_t vendor_config = {};
	vendor_config.mipi_config.dsi_bus = dsi_bus;
	vendor_config.mipi_config.dpi_config = &dpi_config;
	vendor_config.mipi_config.lane_num = MIPI_LANE_NUM;

	esp_lcd_panel_dev_config_t panel_config = {};
	panel_config.reset_gpio_num = RESET_GPIO;
	panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
	panel_config.bits_per_pixel = 16;  // RGB565
	panel_config.vendor_config = &vendor_config;

	ret = esp_lcd_new_panel_ili9881c(panel_io, &panel_config, &panel);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create ILI9881C panel: %s", esp_err_to_name(ret));
		return false;
	}

	// 4. Reset and initialize panel
	ret = esp_lcd_panel_reset(panel);
	if (ret == ESP_OK) {
		vTaskDelay(pdMS_TO_TICKS(10));
		ret = esp_lcd_panel_init(panel);
	}
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to init panel: %s", esp_err_to_name(ret));
		return false;
	}

	// 5. Turn on display
	esp_lcd_panel_disp_on_off(panel, true);
	ESP_LOGI(TAG, "ILI9881C panel initialized");

	// 6. Initialize LVGL
	lv_init();

	// Allocate display buffers (use PSRAM for larger buffers)
	uint32_t buffer_size = H_RES * 100;  // 100 lines buffer
	buf1 = (lv_color_t*)heap_caps_malloc(buffer_size * sizeof(lv_color_t),
		MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	buf2 = (lv_color_t*)heap_caps_malloc(buffer_size * sizeof(lv_color_t),
		MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

	if (!buf1 || !buf2) {
		ESP_LOGE(TAG, "Failed to allocate display buffers");
		return false;
	}
	ESP_LOGI(TAG, "Display buffers allocated: %d x 2 (%d KB each)", buffer_size,
		buffer_size * sizeof(lv_color_t) / 1024);

	// Create LVGL display
	lvgl_disp = lv_display_create(H_RES, V_RES);
	lv_display_set_buffers(lvgl_disp, buf1, buf2, buffer_size * sizeof(lv_color_t),
		LV_DISPLAY_RENDER_MODE_PARTIAL);
	lv_display_set_flush_cb(lvgl_disp, [](lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
		// In DPI mode, hardware handles refresh automatically
		lv_display_flush_ready(disp);
		});
	lv_display_set_user_data(lvgl_disp, panel);

	ESP_LOGI(TAG, "Display initialized: %dx%d RGB565", H_RES, V_RES);
	return true;
}
