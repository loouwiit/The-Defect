#include "testApp.hpp"
#include "app/appStackManager.hpp"
#include "gamepadIndev/gamepadIndev.hpp"
#include "display/font.hpp"
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
		// 背景色 — 根据 value 取色，让每层视觉上可区分
		lv_color_t bg = lv_color_make(
			(m_value * 37) & 0xFF,
			(m_value * 71) & 0xFF,
			(m_value * 113) & 0xFF);
		lv_obj_set_style_bg_color(screen, bg, 0);
		lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

		// 大号标签显示 ID
		m_label = lv_label_create(screen);
		lv_label_set_text_fmt(m_label, "# %lu", m_value);
		lv_obj_set_style_text_font(m_label, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
		lv_obj_set_style_text_color(m_label, lv_color_hex(0xFFFFFF), 0);
		lv_obj_center(m_label);

		// 提示文字
		m_hint = lv_label_create(screen);
		lv_label_set_text(m_hint, "ENTER: push   ESC: pop");
		lv_obj_set_style_text_font(m_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(m_hint, lv_color_hex(0xCCCCCC), 0);
		lv_obj_align(m_hint, LV_ALIGN_BOTTOM_MID, 0, -30);

		// 创建 LVGL 焦点组（独立组，只有此 app 的 indev 绑定到此组）
		m_group = lv_group_create();
		lv_group_add_obj(m_group, m_label);
		lv_group_add_obj(m_group, m_hint);
		lv_obj_add_event_cb(m_label, on_key_cb, LV_EVENT_KEY, this);
		lv_obj_add_event_cb(m_hint,  on_key_cb, LV_EVENT_KEY, this);

		// 将玩家 0 绑定到此组（其他玩家不干涉）
		GamepadIndev::instance().bindGroup(0, m_group);

		lv_group_focus_obj(m_label);
	}
}

void TestApp::deinit()
{
	if (m_group)
	{
		lv_group_delete(m_group);
		m_group = nullptr;
	}
	ESP_LOGI(TAG, "TestApp deinit (value=%lu)", m_value);
	vTaskDelay(3000);
	App::deinit();
}

void TestApp::onForeground()
{
	// 无需手动 debounce，LVGL 的 long press repeat 已处理
}

// ════════════════════════════════════════════════════════════════
// LVGL 按键回调（代替 onGamepadInput）
// ════════════════════════════════════════════════════════════════

void TestApp::on_key_cb(lv_event_t* e)
{
	auto self = static_cast<TestApp*>(lv_event_get_user_data(e));
	if (!self) return;

	uint32_t key = lv_event_get_key(e);

	switch (key)
	{
	case LV_KEY_ENTER:
		ESP_LOGI(TAG, "ENTER — push new layer");
		self->pushApp(new TestApp(self->display, esp_random()));
		break;

	case LV_KEY_HOME:
	case LV_KEY_ESC:
		ESP_LOGI(TAG, "ESC/HOME — pop");
		self->popApp();
		break;

	default:
		break;
	}
}
