#include "testApp.hpp"
#include "app/appStackManager.hpp"
#include "touch/gt911Indev.hpp"
#include "gui/gui.hpp"
#include "esp_log.h"
#include "esp_random.h"

TestApp::TestApp(Display* display, uint32_t value) :
	App(display),
	m_value(value)
{
	ESP_LOGI(TAG, "TestApp created (value=%lu)", value);
}

TestApp::~TestApp()
{
	ESP_LOGI(TAG, "TestApp deleted (value=%lu)", m_value);
}

void TestApp::init()
{
	App::init();

	if (auto guard = display->lockGuard())
	{
		lv_obj_set_style_bg_color(screen, GUI::Color::BG, 0);
		lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

		// 分屏参考线（同屏多人左/右分界）
		lv_obj_t* line = lv_obj_create(screen);
		lv_obj_remove_style_all(line);
		lv_obj_set_size(line, 2, LV_PCT(100));
		lv_obj_set_style_bg_color(line, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
		lv_obj_align(line, LV_ALIGN_LEFT_MID, 360, 0);

		// 5 个手指标记（圆点 + 槽号:trackId 标签）
		static const lv_color_t colors[5] = {
			GUI::Color::DANGER, GUI::Color::SUCCESS,
			GUI::Color::PRIMARY, GUI::Color::WARNING,
			lv_color_hex(0xE040FB)
		};
		for (int i = 0; i < 5; i++)
		{
			auto* dot = lv_obj_create(screen);
			lv_obj_remove_style_all(dot);
			lv_obj_set_size(dot, 36, 36);
			lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
			lv_obj_set_style_bg_color(dot, colors[i], 0);
			lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(dot, 2, 0);
			lv_obj_set_style_border_color(dot, GUI::Color::TEXT, 0);
			lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);   // 默认隐藏（无手指）
			m_markers[i].dot = dot;

			auto* tag = lv_label_create(dot);
			lv_label_set_text_fmt(tag, "%d", i);
			lv_obj_set_style_text_color(tag, GUI::Color::TEXT, 0);
			lv_obj_center(tag);
			m_markers[i].tag = tag;
		}

		// 提示
		lv_obj_t* hint = lv_label_create(screen);
		lv_label_set_text(hint, "5指多指测试  A:push  B:pop");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
	}

	// 60fps 刷新任务（轮询 GT911Indev 槽位 → 更新圆点）
	m_tick = Task::addTask(tickCb, "testTouch", this, 16, Task::Affinity::None);
}

void TestApp::deinit()
{
	if (m_tick)
	{
		Task::removeTask(m_tick);
		m_tick = nullptr;
	}
	App::deinit();
}

void TestApp::onForeground()
{
	nextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
}

TickType_t TestApp::tickCb(void* param)
{
	static_cast<TestApp*>(param)->refresh();
	return 16;   // ~60fps
}

void TestApp::refresh()
{
	if (!running) return;

	if (auto guard = display->lockGuard())
	{
		GT911Indev::SlotInfo slots[5];
		GT911Indev::instance().getSlots(slots, 5);

		for (int i = 0; i < 5; i++)
		{
			lv_obj_t* dot = m_markers[i].dot;
			const auto& info = slots[i];
			if (info.active)
			{
				lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
				lv_obj_set_pos(dot, info.point.x - 18, info.point.y - 18);
				lv_obj_set_style_border_width(dot, info.pressed ? 4 : 1, 0);
				lv_label_set_text_fmt(m_markers[i].tag, "%d:%d%s", i, info.trackId, info.pressed ? "" : "R");
			}
			else
			{
				lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════
// BLE 手柄输入
// ════════════════════════════════════════════════════════════════

void TestApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	if (state.isPressed(GamepadButton::BTN_A))
	{
		if (nextAppChangeTime < xTaskGetTickCount())
		{
			ESP_LOGI(TAG, "A pressed — push new layer");
			pushApp(new TestApp(display, esp_random()));
			nextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
		}
		else ESP_LOGW(TAG, "push too fast, ignoring");
	}

	if (state.isPressed(GamepadButton::BTN_B) || state.isPressed(GamepadButton::BTN_L3))
	{
		if (nextAppChangeTime < xTaskGetTickCount())
		{
			ESP_LOGI(TAG, "B pressed — pop");
			popApp();
			nextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
		}
		else ESP_LOGW(TAG, "pop too fast, ignoring");
	}
}
