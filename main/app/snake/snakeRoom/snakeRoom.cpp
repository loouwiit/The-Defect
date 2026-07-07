#include "snakeRoom.hpp"
#include "app/snake/snakeGame/snakeGame.hpp"
#include "display/font.hpp"
#include "display/display.hpp"
#include "app/appStackManager.hpp"
#include "gui/gui.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "lvgl.h"

// ============================================================
// 颜色
// ============================================================
namespace
{
	constexpr uint32_t BG = 0xff0a0a1e;
	constexpr uint32_t TEXT = 0xffffffff;
	constexpr uint32_t SUBTLE = 0xff888899;
	constexpr uint32_t BTN_1P = 0xff00c853;  // 绿 — P1
	constexpr uint32_t BTN_2P = 0xff448aff;  // 蓝 — P2
	constexpr uint32_t BTN_3P = 0xffffa726;  // 橙 — P3
	constexpr uint32_t BTN_4P = 0xffce93d8;  // 紫 — P4
	constexpr uint32_t HINT = 0xff555566;
}

// ============================================================
// SnakeRoom
// ============================================================

SnakeRoom::SnakeRoom(Display* display)
	: App(display)
{
}

SnakeRoom::~SnakeRoom() = default;

void SnakeRoom::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法锁定显示");
		return;
	}

	// 背景
	lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	createMenu(screen);
}

void SnakeRoom::createMenu(lv_obj_t* parent)
{
	// ── 顶部栏：返回 ──
	auto topBar = GUI::createFlex(parent, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_left(topBar, 16, 0);
	lv_obj_set_style_pad_right(topBar, 16, 0);

	m_backBtn = GUI::createButton(topBar, "← 返回", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	// 初始聚焦
	m_focusIdx = 1;

	// 标题
	auto title = lv_label_create(parent);
	lv_label_set_text(title, "贪吃蛇");
	lv_obj_set_style_text_color(title, lv_color_hex(TEXT), 0);
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

	// 副标题
	auto sub = lv_label_create(parent);
	lv_label_set_text(sub, "选择游戏模式");
	lv_obj_set_style_text_color(sub, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(sub, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 140);

	// 按钮
	auto mkBtn = [&](lv_obj_t*& btn, const char* text, uint32_t color, lv_event_cb_t cb, int yOff)
		{
			btn = lv_button_create(parent);
			lv_obj_set_size(btn, 280, 76);
			lv_obj_set_style_radius(btn, 20, 0);
			lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
			lv_obj_set_style_shadow_width(btn, 16, 0);
			lv_obj_set_style_shadow_color(btn, lv_color_hex(color), 0);
			lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
			lv_obj_set_style_border_width(btn, 0, 0);
			lv_obj_set_style_outline_width(btn, 4, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_color(btn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
			lv_obj_set_style_outline_opa(btn, LV_OPA_60, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_pad(btn, 3, LV_STATE_FOCUSED);
			lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, yOff);

			auto lbl = lv_label_create(btn);
			lv_label_set_text(lbl, text);
			lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
			lv_obj_set_style_text_font(lbl, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
			lv_obj_center(lbl);
			lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
		};

	mkBtn(m_btn1P, "1 人游戏", BTN_1P, btn1PCb, 210);
	mkBtn(m_btn2P, "2 人游戏", BTN_2P, btn2PCb, 310);
	mkBtn(m_btn3P, "3 人游戏", BTN_3P, btn3PCb, 410);
	mkBtn(m_btn4P, "4 人游戏", BTN_4P, btn4PCb, 510);

	// 底部提示
	auto hint = lv_label_create(parent);
	lv_label_set_text(hint, "P1 绿↗  P2 蓝↖  P3 橙↘  P4 紫↗");
	lv_obj_set_style_text_color(hint, lv_color_hex(HINT), 0);
	lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ============================================================
// 按钮回调
// ============================================================

void SnakeRoom::onBackBtnCb(lv_event_t* e)
{
	auto self = static_cast<SnakeRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	// LVGL 事件回调中持锁，栈操作须延后
	Task::addTask([](void* param) -> TickType_t
		{
			auto* room = static_cast<SnakeRoom*>(param);
			if (room->getManager())
				room->popApp();
			return Task::infinityTime;
		}, "snakeRoomBack", self, 0, Task::Affinity::None);
}

void SnakeRoom::btn1PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{

		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_COOLDOWN;

	Task::addTask([](void* p) -> TickType_t {
		auto* room = static_cast<SnakeRoom*>(p);
		room->pushApp(new SnakeGame(room->display, 1));
		return Task::infinityTime;
		}, "start1P", self, 0, Task::Affinity::None);
}

void SnakeRoom::btn2PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{

		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_COOLDOWN;

	Task::addTask([](void* p) -> TickType_t {
		auto* room = static_cast<SnakeRoom*>(p);
		room->pushApp(new SnakeGame(room->display, 2));
		return Task::infinityTime;
		}, "start2P", self, 0, Task::Affinity::None);
}

void SnakeRoom::btn3PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{

		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_COOLDOWN;

	Task::addTask([](void* p) -> TickType_t {
		auto* room = static_cast<SnakeRoom*>(p);
		room->pushApp(new SnakeGame(room->display, 3));
		return Task::infinityTime;
		}, "start3P", self, 0, Task::Affinity::None);
}

void SnakeRoom::btn4PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{

		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_COOLDOWN;

	Task::addTask([](void* p) -> TickType_t {
		auto* room = static_cast<SnakeRoom*>(p);
		room->pushApp(new SnakeGame(room->display, 4));
		return Task::infinityTime;
		}, "start4P", self, 0, Task::Affinity::None);
}

// ============================================================
// 焦点导航
// ============================================================

void SnakeRoom::applyFocus()
{
	auto clear = [](lv_obj_t* obj) { if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj) { if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clear(m_backBtn);
	clear(m_btn1P);
	clear(m_btn2P);
	clear(m_btn3P);
	clear(m_btn4P);

	switch (m_focusIdx)
	{
	case 0: focus(m_backBtn); break;
	case 1: focus(m_btn1P);   break;
	case 2: focus(m_btn2P);   break;
	case 3: focus(m_btn3P);   break;
	case 4: focus(m_btn4P);   break;
	}
}

void SnakeRoom::activateFocus()
{
	switch (m_focusIdx)
	{
	case 0: lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr); break;
	case 1: lv_obj_send_event(m_btn1P, LV_EVENT_CLICKED, nullptr); break;
	case 2: lv_obj_send_event(m_btn2P, LV_EVENT_CLICKED, nullptr); break;
	case 3: lv_obj_send_event(m_btn3P, LV_EVENT_CLICKED, nullptr); break;
	case 4: lv_obj_send_event(m_btn4P, LV_EVENT_CLICKED, nullptr); break;
	}
}

// ============================================================
// BLE 手柄输入
// ============================================================

void SnakeRoom::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 边沿检测：仅刚按下的按钮有效 ──
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// ── B：返回 ──
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (xTaskGetTickCount() < m_nextActionTime)
		{
			ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", m_nextActionTime - xTaskGetTickCount());
			return;
		}
		m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN;

		Task::addTask([](void* param) -> TickType_t {
			auto* room = static_cast<SnakeRoom*>(param);
			if (room->getManager()) room->popApp();
			return Task::infinityTime;
			}, "snakeRoomBack", this, 0, Task::Affinity::None);
		return;
	}

	// ── A / L3：激活聚焦 ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		// 各个按钮有按钮重复检测，这里不再检测，否则导致判定永远失败。
		activateFocus();
		return;
	}

	// ── 摇杆归位判断 ──
	assert(playerId < MaxPlayerCount);
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[playerId] = 0;
		return;
	}
	if (xTaskGetTickCount() < m_nextMoveTime[playerId]) return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	// ── 上下导航 ──
	if (lyUp && m_focusIdx > 0)   m_focusIdx--;
	if (lyDown && m_focusIdx < 4) m_focusIdx++;

	auto guard = display->lockGuard();
	applyFocus();
}

void SnakeRoom::onForeground()
{
	// 全置按下：下一帧 newPress = state.buttons & ~0xFFFF = 0，自动跳过
	m_prevButtons = 0xFFFF;
	applyFocus();

	m_nextActionTime = xTaskGetTickCount() + ACTION_COOLDOWN;
}
