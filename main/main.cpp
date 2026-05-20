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

	// 3. 使用 LVGL API 绘制界面
	if (display.lock()) {
		lv_obj_t* label = lv_label_create(lv_scr_act());
		lv_label_set_text(label, "Hello LVGL!");
		lv_obj_center(label);
		display.unlock();
	}

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
