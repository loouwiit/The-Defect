#include "testApp.hpp"
#include "app/appStackManager.hpp"
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
		lv_label_set_text(m_hint, "A: push   B: pop");
		lv_obj_set_style_text_font(m_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(m_hint, lv_color_hex(0xCCCCCC), 0);
		lv_obj_align(m_hint, LV_ALIGN_BOTTOM_MID, 0, -30);
	}
}

void TestApp::deinit()
{
	ESP_LOGI(TAG, "TestApp deinit (value=%lu)", m_value);
	vTaskDelay(3000);
	App::deinit();
}

void TestApp::onForeground()
{
	NextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
}

// ════════════════════════════════════════════════════════════════
// BLE 手柄输入
// ════════════════════════════════════════════════════════════════

void TestApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	if (state.isPressed(GamepadButton::BTN_A))
	{
		if (NextAppChangeTime < xTaskGetTickCount())
		{
			ESP_LOGI(TAG, "A pressed — push new layer");
			pushApp(new TestApp(display, esp_random()));
			NextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
		}
		else ESP_LOGW(TAG, "push too fast, ignoring");
	}

	if (state.isPressed(GamepadButton::BTN_L3))
	{
		if (NextAppChangeTime < xTaskGetTickCount())
		{
			ESP_LOGI(TAG, "BTN_L3 pressed — pop");
			popApp();
			NextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
		}
		else ESP_LOGW(TAG, "pop too fast, ignoring");
	}
}
