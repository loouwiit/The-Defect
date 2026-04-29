#include <esp_log.h>
#include <esp_event.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "task/task.hpp"
#include "display/display.hpp"

static const char* TAG = "main";

extern "C" void app_main(void)
{
	ESP_LOGI(TAG, "Initializing display...");

	// Create display object
	Display* display = new Display();
	if (!display->init()) {
		ESP_LOGE(TAG, "Display initialization failed!");
		return;
	}

	ESP_LOGI(TAG, "Display initialized successfully!");

	// Create a simple label showing "Hello World"
	lv_obj_t* label = lv_label_create(lv_screen_active());
	lv_label_set_text(label, "Hello World!");
	lv_obj_center(label);
	lv_obj_set_style_text_color(label, lv_color_make(255, 255, 255), 0);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);

	ESP_LOGI(TAG, "Hello World displayed!");

	// Main loop - LVGL needs periodic tick handling
	while (true) {
		lv_timer_handler();
		vTaskDelay(pdMS_TO_TICKS(5));
	}

	// Never reaches here
	delete display;
}
