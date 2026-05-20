#include <esp_log.h>
#include <esp_event.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "task/task.hpp"
#include "display/display.hpp"
#include "gui/gui.hpp"

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

	GUI::setBackground(lv_color_hex(0x0000F0));

	lv_obj_t* page = GUI::createPage();

	lv_obj_t* card = GUI::createCard(page, 300, 150);

	lv_obj_t* title = GUI::createTitle(page, "Hello, LVGL!");
	lv_obj_set_pos(title, 200, 200);

	lv_obj_t* value = GUI::createValue(page, "42");
	lv_obj_set_pos(value, 400, 200);

	lv_obj_t* progress = GUI::createProgressBar(page, lv_pct(100), 0, 100, 75);
	lv_obj_set_pos(progress, 200, 300);

	lv_obj_t* sw = GUI::createSwitch(page);
	lv_obj_set_pos(sw, 200, 400);

	lv_obj_t* slider = GUI::createSlider(page, lv_pct(100), 0, 100, 50);
	lv_obj_set_pos(slider, 200, 500);

	// Main loop - LVGL needs periodic tick handling
	while (true) {
		lv_timer_handler();
		vTaskDelay(pdMS_TO_TICKS(5));
	}

	// Never reaches here
	delete display;
}
