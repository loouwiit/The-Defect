#include "testApp.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "touch/touchService.hpp"
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

		lv_anim_t a;
		lv_anim_init(&a);
		lv_anim_set_var(&a, m_hint);
		lv_anim_set_values(&a, 0, 100);
		lv_anim_set_time(&a, 20);
		lv_anim_set_playback_time(&a, 20);
		lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
		lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
			lv_obj_set_style_bg_color(static_cast<lv_obj_t*>(obj), v > 10 ? lv_color_hex(0xFFFFFFFF) : lv_color_hex(0xCCCCCC), 0);
			});
		lv_anim_start(&a);

		// ── 多指可视化试验田 ──
		// 状态标签（左上角，实时显示每指 id/坐标/状态）
		m_touchInfo = lv_label_create(screen);
		lv_obj_set_style_text_font(m_touchInfo, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(m_touchInfo, lv_color_hex(0x00FF88), 0);
		lv_obj_set_pos(m_touchInfo, 12, 12);
		lv_label_set_text(m_touchInfo, "Fingers: 0");

		// 手指位置圆点（0-4，颜色区分）
		static const uint32_t dotColors[5] = { 0xFFFFFF, 0x00FF88, 0x4488FF, 0xFFA800, 0xFF55CC };
		for (int i = 0; i < 5; i++)
		{
			m_dots[i] = lv_obj_create(screen);
			lv_obj_set_size(m_dots[i], 18, 18);
			lv_obj_set_style_radius(m_dots[i], LV_RADIUS_CIRCLE, 0);
			lv_obj_set_style_bg_color(m_dots[i], lv_color_hex(dotColors[i]), 0);
			lv_obj_set_style_bg_opa(m_dots[i], LV_OPA_70, 0);
			lv_obj_set_style_border_width(m_dots[i], 1, 0);
			lv_obj_set_style_border_color(m_dots[i], lv_color_hex(0xFFFFFFFF), 0);
			lv_obj_add_flag(m_dots[i], LV_OBJ_FLAG_HIDDEN);
		}

		// 4 分屏分隔线（中央十字）
		lv_obj_t* lineV = lv_obj_create(screen);
		lv_obj_set_size(lineV, 1, LV_VER_RES);
		lv_obj_set_pos(lineV, LV_HOR_RES / 2, 0);
		lv_obj_set_style_bg_color(lineV, lv_color_hex(0x33FFFFFF), 0);
		lv_obj_set_style_border_width(lineV, 0, 0);
		lv_obj_t* lineH = lv_obj_create(screen);
		lv_obj_set_size(lineH, LV_HOR_RES, 1);
		lv_obj_set_pos(lineH, 0, LV_VER_RES / 2);
		lv_obj_set_style_bg_color(lineH, lv_color_hex(0x33FFFFFF), 0);
		lv_obj_set_style_border_width(lineH, 0, 0);
	}

	// 启动多指演示任务
	startTouchDemo();
}

void TestApp::deinit()
{
	ESP_LOGI(TAG, "TestApp deinit (value=%lu)", m_value);
	stopTouchDemo();
	vTaskDelay(3000);
	App::deinit();
}

void TestApp::onForeground()
{
	nextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
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

	if (state.isPressed(GamepadButton::BTN_L3))
	{
		if (nextAppChangeTime < xTaskGetTickCount())
		{
			ESP_LOGI(TAG, "BTN_L3 pressed — pop");
			popApp();
			nextAppChangeTime = xTaskGetTickCount() + 500; // 500 ms delay
		}
		else ESP_LOGW(TAG, "pop too fast, ignoring");
	}
}

// ════════════════════════════════════════════════════════════════
// 多指可视化试验田
// ════════════════════════════════════════════════════════════════

void TestApp::startTouchDemo()
{
	if (m_touchTask) return;
	m_touchTask = Task::addTask(touchDemoTick, "touchDemo", this, 0, Task::Affinity::None);
}

void TestApp::stopTouchDemo()
{
	if (m_touchTask)
	{
		Task::removeTask(m_touchTask);
		m_touchTask = nullptr;
	}
}

TickType_t TestApp::touchDemoTick(void* param)
{
	static_cast<TestApp*>(param)->updateTouchDemo();
	return 16;   // 60fps
}

void TestApp::updateTouchDemo()
{
	// 更新多指追踪（内部持 LVGL 锁；试验田无注册按钮）
	TouchService::instance().update();

	auto guard = display->lockGuard();
	if (!guard) return;

	const auto& tr = TouchService::instance().tracker();

	char buf[192];
	int len = snprintf(buf, sizeof(buf), "Fingers: %d\n", tr.fingerCount());
	for (int i = 0; i < tr.fingerCount() && i < 5; i++)
	{
		const auto* f = tr.fingerAt(i);
		if (!f) continue;
		len += snprintf(buf + len, sizeof(buf) - len, "F%d id=%d (%d,%d) %s%s%s\n",
			i, f->id, f->x, f->y,
			f->justPressed ? "DOWN " : "",
			f->pressed ? "HOLD " : "",
			f->justReleased ? "UP" : "");

		if (m_dots[i])
		{
			lv_obj_set_pos(m_dots[i], f->x - 9, f->y - 9);
			lv_obj_clear_flag(m_dots[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
	for (int i = tr.fingerCount(); i < 5; i++)
		if (m_dots[i]) lv_obj_add_flag(m_dots[i], LV_OBJ_FLAG_HIDDEN);

	if (m_touchInfo)
		lv_label_set_text(m_touchInfo, buf);
}
