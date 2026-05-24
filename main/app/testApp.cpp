#include "testApp.hpp"
#include "esp_log.h"

TestApp::TestApp(Display* display) :
	App(display),
	fps{ lv_label_create(screen) }
{
	ESP_LOGI(TAG, "TestApp created");

	lv_label_set_text(fps, "Hello from TestApp!");
	lv_obj_center(fps);
}

TestApp::~TestApp()
{
	ESP_LOGI(TAG, "TestApp deleted");
}

void TestApp::init()
{
	App::init();
	thread = Thread{ backgroundMain, "TestApp", this };
}

void TestApp::deinit()
{
	running = false;
}

void TestApp::backgroundMain(void* param)
{
	auto& self = *(TestApp*)param;
	while (self.running)
	{
		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", self.display->getFps());

		if (auto guard = self.display->lockGuard())
		{
			lv_label_set_text_fmt(self.fps, "fps: %ld", self.display->getFps());
			lv_obj_set_style_text_color(self.fps, lv_color_hex(0xFF0000), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", self.display->getFps());

		if (auto guard = self.display->lockGuard())
		{
			lv_label_set_text_fmt(self.fps, "fps: %ld", self.display->getFps());
			lv_obj_set_style_text_color(self.fps, lv_color_hex(0x00FF00), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		ESP_LOGI(TAG, "FPS: %d", self.display->getFps());

		if (auto guard = self.display->lockGuard())
		{
			lv_label_set_text_fmt(self.fps, "fps: %ld", self.display->getFps());
			lv_obj_set_style_text_color(self.fps, lv_color_hex(0x0000FF), 0);
		}
	}

	self.deletable = true;
	self.thread = {};
}
