#include <esp_log.h>
#include <esp_event.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "task/task.hpp"
#include "display/display.hpp"
#include "esp_brookesia.hpp"

#warning AI

static const char* TAG = "main";

// Global display object
static Display* g_display = nullptr;

// LVGL lock callback for ESP-Brookesia: returns bool, takes int timeout
static bool lvgl_lock_cb(int timeout)
{
	// LVGL lock - in ESP-IDF with lvgl_port, this would call lvgl_port_lock(timeout)
	// For now, we rely on single-task LVGL handling
	return true;
}

// LVGL unlock callback for ESP-Brookesia: no params, no return
static void lvgl_unlock_cb()
{
	// LVGL unlock - in ESP-IDF with lvgl_port, this would call lvgl_port_unlock()
}

extern "C" void app_main(void)
{
	ESP_LOGI(TAG, "Initializing display...");

	// Create display object
	g_display = new Display();
	if (!g_display->init()) {
		ESP_LOGE(TAG, "Display initialization failed!");
		return;
	}

	ESP_LOGI(TAG, "Display initialized successfully!");

	// Get LVGL display handle
	lv_display_t* lvgl_disp = g_display->getLvglDisplay();

	// Create ESP-Brookesia Phone object
	ESP_LOGI(TAG, "Creating ESP-Brookesia Phone...");
	ESP_Brookesia_Phone* phone = new ESP_Brookesia_Phone(lvgl_disp);
	if (phone == nullptr) {
		ESP_LOGE(TAG, "Failed to create ESP-Brookesia Phone!");
		return;
	}

	// Try to use stylesheet for 800x1280 resolution
	ESP_Brookesia_PhoneStylesheet_t* stylesheet = new ESP_Brookesia_PhoneStylesheet_t(ESP_BROOKESIA_PHONE_800_1280_DARK_STYLESHEET());
	if (stylesheet != nullptr) {
		ESP_LOGI(TAG, "Using stylesheet: %s", stylesheet->core.name);
		if (!phone->addStylesheet(stylesheet)) {
			ESP_LOGW(TAG, "Failed to add stylesheet");
		} else if (!phone->activateStylesheet(stylesheet)) {
			ESP_LOGW(TAG, "Failed to activate stylesheet");
		}
		delete stylesheet;
	}

	// Register LVGL lock/unlock callbacks
	phone->registerLvLockCallback(lvgl_lock_cb, 0);
	phone->registerLvUnlockCallback(lvgl_unlock_cb);

	// Start the phone system
	ESP_LOGI(TAG, "Starting ESP-Brookesia Phone...");
	if (!phone->begin()) {
		ESP_LOGE(TAG, "Failed to begin ESP-Brookesia Phone!");
		return;
	}

	// Install example apps
	ESP_LOGI(TAG, "Installing apps...");

	// Simple Conf App
	PhoneAppSimpleConf* app_simple_conf = new PhoneAppSimpleConf();
	if (app_simple_conf != nullptr) {
		if (phone->installApp(app_simple_conf) >= 0) {
			ESP_LOGI(TAG, "Simple Conf app installed");
		} else {
			ESP_LOGW(TAG, "Failed to install Simple Conf app");
			delete app_simple_conf;
		}
	}

	// Complex Conf App
	PhoneAppComplexConf* app_complex_conf = new PhoneAppComplexConf();
	if (app_complex_conf != nullptr) {
		if (phone->installApp(app_complex_conf) >= 0) {
			ESP_LOGI(TAG, "Complex Conf app installed");
		} else {
			ESP_LOGW(TAG, "Failed to install Complex Conf app");
			delete app_complex_conf;
		}
	}

	// Squareline App
	PhoneAppSquareline* app_squareline = PhoneAppSquareline::getInstance();
	if (app_squareline != nullptr) {
		if (phone->installApp(app_squareline) >= 0) {
			ESP_LOGI(TAG, "Squareline app installed");
		} else {
			ESP_LOGW(TAG, "Failed to install Squareline app");
		}
	}

	ESP_LOGI(TAG, "ESP-Brookesia Phone started successfully!");

	// Main loop - LVGL needs periodic tick handling
	while (true) {
		lv_timer_handler();
		vTaskDelay(pdMS_TO_TICKS(5));
	}

	// Never reaches here
	delete phone;
	delete g_display;
}
