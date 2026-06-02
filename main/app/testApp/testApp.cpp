#include "testApp.hpp"
#include "esp_log.h"

TestApp::TestApp(Display* display) :
	App(display)
{
	ESP_LOGI(TAG, "TestApp created");
}

TestApp::~TestApp()
{
	ESP_LOGI(TAG, "TestApp deleted");
}

void TestApp::init()
{
	App::init();

	// LVGL相关必须上锁
	if (auto guard = display->lockGuard())
	{
		// 创建一个标签显示FPS
		fps = lv_label_create(screen);
		ESP_LOGI(TAG, "fps created at %p", fps);
		lv_label_set_text(fps, "Hello from TestApp!");
		lv_obj_center(fps); // 水平居中

		// 设置屏幕按下后背景变蓝
		lv_obj_set_style_bg_color(screen, lv_color_hex(0x0000FF), LV_STATE_PRESSED);

		// 按下事件
		lv_obj_add_event_cb(screen, [](lv_event_t* e)
			{
				TestApp& self = *(TestApp*)lv_event_get_user_data(e);

				if (auto indev = lv_event_get_indev(e))
				{
					lv_point_t point;
					lv_indev_get_point(indev, &point);
					ESP_LOGI(TAG, "pressed at (%d, %d)", point.x, point.y);

					// 回调内无须上锁
					lv_obj_set_align(self.fps, LV_ALIGN_TOP_LEFT);
					lv_obj_set_pos(self.fps, point.x, point.y);
				}
			}, LV_EVENT_PRESSED, this);

		// 移动事件
		lv_obj_add_event_cb(screen, [](lv_event_t* e)
			{
				TestApp& self = *(TestApp*)lv_event_get_user_data(e);

				if (auto indev = lv_event_get_indev(e))
				{
					lv_point_t point;
					lv_indev_get_point(indev, &point);
					ESP_LOGI(TAG, "pressing at (%d, %d)", point.x, point.y);

					lv_obj_set_pos(self.fps, point.x, point.y);
				}
			}, LV_EVENT_PRESSING, this);

		// 释放事件
		lv_obj_add_event_cb(screen, [](lv_event_t* e)
			{
				auto& self = *(TestApp*)lv_event_get_user_data(e);

				if (auto indev = lv_event_get_indev(e))
				{
					lv_point_t point;
					lv_indev_get_point(indev, &point);
					ESP_LOGI(TAG, "released at (%d, %d)", point.x, point.y);

					lv_obj_align(self.fps, LV_ALIGN_CENTER, 0, 0);
				}
			}, LV_EVENT_RELEASED, this);
	}

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
		// ESP_LOGI(TAG, "FPS: %d", self.display->getFps());

		if (auto guard = self.display->lockGuard())
		{
			lv_label_set_text_fmt(self.fps, "fps: %ld", self.display->getFps());
			lv_obj_set_style_text_color(self.fps, lv_color_hex(0xFF0000), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		// ESP_LOGI(TAG, "FPS: %d", self.display->getFps());

		if (auto guard = self.display->lockGuard())
		{
			lv_label_set_text_fmt(self.fps, "fps: %ld", self.display->getFps());
			lv_obj_set_style_text_color(self.fps, lv_color_hex(0x00FF00), 0);
		}

		vTaskDelay(pdMS_TO_TICKS(10));
		// ESP_LOGI(TAG, "FPS: %d", self.display->getFps());

		if (auto guard = self.display->lockGuard())
		{
			lv_label_set_text_fmt(self.fps, "fps: %ld", self.display->getFps());
			lv_obj_set_style_text_color(self.fps, lv_color_hex(0x0000FF), 0);
		}
	}

	self.deletable = true;
	self.thread = {};
}
