#include "chineseChessRoom.hpp"
#include "chineseChessApp.hpp"
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
	constexpr uint32_t BTN_AI = 0xffd4a574;  // 红木色 — 人机
	constexpr uint32_t BTN_2P = 0xff448aff;  // 蓝色 — 双人
	constexpr uint32_t HINT = 0xff555566;
}

// ============================================================
// ChineseChessRoom
// ============================================================

ChineseChessRoom::ChineseChessRoom(Display* display)
	: App(display)
{
}

ChineseChessRoom::~ChineseChessRoom() = default;

void ChineseChessRoom::init()
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

void ChineseChessRoom::createMenu(lv_obj_t* parent)
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

	m_focusIdx = 1;
	m_prevButtons = 0xFFFF;

	// 标题
	auto title = lv_label_create(parent);
	lv_label_set_text(title, "中国象棋");
	lv_obj_set_style_text_color(title, lv_color_hex(TEXT), 0);
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

	// 副标题
	auto sub = lv_label_create(parent);
	lv_label_set_text(sub, "选择游戏模式");
	lv_obj_set_style_text_color(sub, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(sub, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 140);

	// 按钮工厂
	auto mkBtn = [&](lv_obj_t*& btn, const char* text, const char* desc, uint32_t color, lv_event_cb_t cb, int yOff)
	{
		btn = lv_button_create(parent);
		lv_obj_set_size(btn, 300, 80);
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

	mkBtn(m_btnAI, "人机对战",  "挑战电脑", BTN_AI, btnAICb, 220);
	mkBtn(m_btn2P, "双人对战",  "好友对弈", BTN_2P, btn2PCb, 330);

	// 底部提示
	auto hint = lv_label_create(parent);
	lv_label_set_text(hint, "红方先走  触摸选子  摇杆导航");
	lv_obj_set_style_text_color(hint, lv_color_hex(HINT), 0);
	lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ============================================================
// 按钮回调
// ============================================================

void ChineseChessRoom::onBackBtnCb(lv_event_t* e)
{
	auto self = static_cast<ChineseChessRoom*>(lv_event_get_user_data(e));
	Task::addTask([](void* param) -> TickType_t
		{
			auto* room = static_cast<ChineseChessRoom*>(param);
			if (room->getManager())
				room->popApp();
			return Task::infinityTime;
		}, "chessRoomBack", self, 0, Task::Affinity::None);
}

void ChineseChessRoom::btnAICb(lv_event_t* e)
{
	auto self = static_cast<ChineseChessRoom*>(lv_event_get_user_data(e));
	Task::addTask([](void* p) -> TickType_t {
		auto* room = static_cast<ChineseChessRoom*>(p);
		room->pushApp(new ChineseChessApp(room->display, 0));
		return Task::infinityTime;
		}, "startChessAI", self, 0, Task::Affinity::None);
}

void ChineseChessRoom::btn2PCb(lv_event_t* e)
{
	auto self = static_cast<ChineseChessRoom*>(lv_event_get_user_data(e));
	Task::addTask([](void* p) -> TickType_t {
		auto* room = static_cast<ChineseChessRoom*>(p);
		room->pushApp(new ChineseChessApp(room->display, 1));
		return Task::infinityTime;
		}, "startChess2P", self, 0, Task::Affinity::None);
}

// ============================================================
// 焦点导航
// ============================================================

void ChineseChessRoom::applyFocus()
{
	auto clear = [](lv_obj_t* obj) { if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj) { if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clear(m_backBtn);
	clear(m_btnAI);
	clear(m_btn2P);

	switch (m_focusIdx)
	{
	case 0: focus(m_backBtn); break;
	case 1: focus(m_btnAI);   break;
	case 2: focus(m_btn2P);   break;
	}
}

void ChineseChessRoom::activateFocus()
{
	switch (m_focusIdx)
	{
	case 0: lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr); break;
	case 1: lv_obj_send_event(m_btnAI,   LV_EVENT_CLICKED, nullptr); break;
	case 2: lv_obj_send_event(m_btn2P,   LV_EVENT_CLICKED, nullptr); break;
	}
}

// ============================================================
// 手柄输入
// ============================================================

void ChineseChessRoom::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// B：返回
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		Task::addTask([](void* param) -> TickType_t {
			auto* room = static_cast<ChineseChessRoom*>(param);
			if (room->getManager()) room->popApp();
			return Task::infinityTime;
			}, "chessRoomBack", this, 0, Task::Affinity::None);
		return;
	}

	// A / L3：激活聚焦
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		activateFocus();
		return;
	}

	// 摇杆归位判断
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime = 0;
		return;
	}
	if (m_nextMoveTime >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime = xTaskGetTickCount() + delay;

	// 上下导航（3 项）
	if (lyUp && m_focusIdx > 0)   m_focusIdx--;
	if (lyDown && m_focusIdx < 2) m_focusIdx++;

	auto guard = display->lockGuard();
	applyFocus();
}

void ChineseChessRoom::onForeground()
{
	m_prevButtons = 0xFFFF;
	applyFocus();
}
