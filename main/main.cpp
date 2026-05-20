#include "display/display.hpp"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr char TAG[] = "main";

extern "C" void app_main(void)
{
	// 1. 初始化显示（硬件 + LVGL 适配器）
	Display display;
	if (!display.init(ESP_LV_ADAPTER_ROTATE_90)) {
		ESP_LOGE(TAG, "Failed to initialize display");
		return;
	}

	// (可选) 在此处注册触摸/按钮等输入设备
	// ...

	// 2. 启动 LVGL 工作任务
	if (!display.start()) {
		ESP_LOGE(TAG, "Failed to start LVGL adapter");
		return;
	}

	// 3. 使用 LVGL API 绘制界面（RAII 自动加锁/解锁）
	lv_obj_t* label{};
	if (auto guard = display.lockGuard())
	{
		label = lv_label_create(lv_scr_act());
		lv_label_set_text(label, "Hello LVGL!");
		lv_obj_center(label);
	} // guard 析构时自动解锁

	esp_lv_adapter_fps_stats_enable(display.getLvglDisplay(), true);

	auto getFps = [disp = display.getLvglDisplay()]()->uint32_t
		{
			uint32_t fps{};
			auto err = esp_lv_adapter_get_fps(disp, &fps);
			ESP_ERROR_CHECK_WITHOUT_ABORT(err);
			return fps;
		};

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", getFps());

		if (auto guard = display.lockGuard())
		{
			lv_label_set_text_fmt(label, "fps: %ld", getFps());
			lv_obj_set_style_text_color(label, lv_color_hex(0xFF0000), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", getFps());

		if (auto guard = display.lockGuard())
		{
			lv_label_set_text_fmt(label, "fps: %ld", getFps());
			lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", getFps());

		if (auto guard = display.lockGuard())
		{
			lv_label_set_text_fmt(label, "fps: %ld", getFps());
			lv_obj_set_style_text_color(label, lv_color_hex(0x0000FF), 0);
		}
	}
}
