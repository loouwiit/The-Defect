#include "display/display.hpp"
#include "touch/touch.hpp"

#include <esp_log.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app/testApp.hpp"

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
	display.setFpsStatisticsEnabled();

	// 3. 启动任务管理器
	Task::init(2);

	// 4. 启动测试应用
	TestApp* app = new TestApp{ &display };
	app->init();
	if (auto guard = display.lockGuard())
	{
		// 应用(v.) 应用(n.) 到屏幕
		display.applyApp(app);
	}

	// 5. 保持栈上变量，后续移除
	while (true)
		vTaskDelay(1000);

	// cleanup (unreachable in this example, but good practice)
	delete std::exchange(app, nullptr);
}
