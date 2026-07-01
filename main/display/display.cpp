#include "display.hpp"
#include "ili9881c.hpp"
#include "app/app.hpp"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_err.h>

static const char* TAG = "Display";

Display::Display() = default;

Display::~Display()
{
	if (lv_disp)
		esp_lv_adapter_deinit();
}

bool Display::init()
{
	esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
	adapter_cfg.task_stack_size = 32 * 1024;
	adapter_cfg.stack_in_psram = true;
	auto ret = esp_lv_adapter_init(&adapter_cfg);
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to init LVGL adapter: %s", esp_err_to_name(ret));
		return false;
	}

	return true;
}

bool Display::bindDisplay(esp_lcd_panel_handle_t lcdPanel, esp_lcd_panel_io_handle_t lcdIo, uint16_t horizontalResolution, uint16_t verticalResolution, esp_lv_adapter_tear_avoid_mode_t tearAvoidMode, esp_lv_adapter_rotation_t rotation)
{
	// 注册显示设备
	esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_MIPI_DEFAULT_CONFIG(
		lcdPanel, lcdIo, horizontalResolution, verticalResolution, rotation);
	disp_cfg.tear_avoid_mode = tearAvoidMode;
	disp_cfg.profile.enable_ppa_accel = true;
	disp_cfg.profile.require_double_buffer = true;

	lv_disp = esp_lv_adapter_register_display(&disp_cfg);
	if (lv_disp == NULL)
	{
		ESP_LOGE(TAG, "Failed to register display with LVGL adapter");
		return false;
	}

	ESP_LOGI(TAG, "Display + LVGL adapter initialized (%dx%d, rotation=%d)",
		horizontalResolution, verticalResolution, (int)rotation);
	return true;
}

bool Display::bindTouch(esp_lcd_touch_handle_t touchHandle)
{
	// 使用默认配置宏注册触摸设备
	esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(lv_disp, touchHandle);
	lv_indev_t* touch = esp_lv_adapter_register_touch(&touch_cfg);
	assert(touch != NULL);
	return touch != NULL;
}

bool Display::start()
{
	esp_err_t ret = esp_lv_adapter_start();
	if (ret != ESP_OK)
	{
		ESP_LOGE(TAG, "Failed to start LVGL adapter: %s", esp_err_to_name(ret));
		return false;
	}
	ESP_LOGI(TAG, "LVGL adapter task started");
	return true;
}

void Display::setFpsStatisticsEnabled(bool enable) const
{
	esp_lv_adapter_fps_stats_enable(lv_disp, enable);
}

uint32_t Display::getFps() const
{
	uint32_t fps{};
	auto err = esp_lv_adapter_get_fps(lv_disp, &fps);
	ESP_ERROR_CHECK_WITHOUT_ABORT(err);
	return fps;
}

void Display::applyApp(App* app) const
{
	if (auto guard = lockGuard())
	{
		lv_screen_load(app->screen);
		activeApp = app;
	}
	else
	{
		ESP_LOGE(TAG, "applyApp: failed to lock display");
	}
}

// ── 背光亮度控制 ──

void Display::setBrightness(int percent)
{
	ILI9881c::getInstance().setBrightness(percent);
}

int Display::getBrightness() const
{
	return ILI9881c::getInstance().getBrightness();
}

void Display::saveBrightness()
{
	ILI9881c::getInstance().saveBrightnessToNvs();
}
