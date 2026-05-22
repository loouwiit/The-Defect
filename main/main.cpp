#include "display/display.hpp"
#include "touch/touch.hpp"
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

	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	Touch touch{ iic, {GPIO_NUM_46} };
	display.bindTouch(touch.getHandle());

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

		auto screen = lv_screen_active();
		lv_obj_add_event_cb(screen, [](lv_event_t* event)
			{
				ESP_LOGI("callback", "LV_EVENT_PRESSED");
			}, LV_EVENT_PRESSED, nullptr);
		lv_obj_add_event_cb(screen, [](lv_event_t* event)
			{
				ESP_LOGI("callback", "LV_EVENT_PRESSING");
			}, LV_EVENT_PRESSING, nullptr);
		lv_obj_add_event_cb(screen, [](lv_event_t* event)
			{
				ESP_LOGI("callback", "LV_EVENT_RELEASED");
			}, LV_EVENT_RELEASED, nullptr);
	} // guard 析构时自动解锁

	display.setFpsStatisticsEnabled();

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", display.getFps());

		if (auto guard = display.lockGuard())
		{
			lv_label_set_text_fmt(label, "fps: %ld", display.getFps());
			lv_obj_set_style_text_color(label, lv_color_hex(0xFF0000), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", display.getFps());

		if (auto guard = display.lockGuard())
		{
			lv_label_set_text_fmt(label, "fps: %ld", display.getFps());
			lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", display.getFps());

		if (auto guard = display.lockGuard())
		{
			lv_label_set_text_fmt(label, "fps: %ld", display.getFps());
			lv_obj_set_style_text_color(label, lv_color_hex(0x0000FF), 0);
		}
	}
}
