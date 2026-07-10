#include "fruitNinjaRoom.hpp"
#include "fruitNinjaApp.hpp"
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
	constexpr uint32_t BTN_GREEN = 0xff00c853;
	constexpr uint32_t BTN_BLUE = 0xff448aff;
	constexpr uint32_t BTN_ORANGE = 0xffffa726;
	constexpr uint32_t HINT = 0xff555566;
	constexpr uint32_t BTN_GRAY = 0xff555566;
	constexpr uint32_t BTN_ACTIVE = 0xff00e676;
	constexpr uint32_t BTN_INACTIVE = 0xff333355;
}

// ============================================================
// FruitNinjaRoom
// ============================================================

FruitNinjaRoom::FruitNinjaRoom(Display* display)
	: App(display)
{
}

FruitNinjaRoom::~FruitNinjaRoom() = default;

void FruitNinjaRoom::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法锁定显示");
		return;
	}

	lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	createMenu(screen);

	m_focusIdx = 1;
	applyFocus();
}

// ============================================================
// 创建菜单
// ============================================================

void FruitNinjaRoom::createMenu(lv_obj_t* parent)
{
	// ── 顶部栏：返回 ──
	auto topBar = GUI::createFlex(parent, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_left(topBar, 16, 0);
	lv_obj_set_style_pad_right(topBar, 16, 0);

	m_backBtn = GUI::createButton(topBar, "\u2190 \u8fd4\u56de", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	// ── 标题 ──
	m_title = lv_label_create(parent);
	lv_label_set_text(m_title, "\u6c34\u679c\u5fcd\u8005");
	lv_obj_set_style_text_color(m_title, lv_color_hex(TEXT), 0);
	lv_obj_set_style_text_font(m_title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(m_title, LV_ALIGN_TOP_MID, 0, 60);

	// ── 模式选择标题 ──
	auto modeTitle = lv_label_create(parent);
	lv_label_set_text(modeTitle, "\u6e38\u620f\u6a21\u5f0f");
	lv_obj_set_style_text_color(modeTitle, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(modeTitle, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_align(modeTitle, LV_ALIGN_TOP_MID, 0, 130);

	// 模式按钮辅助函数
	auto mkModeBtn = [&](lv_obj_t*& btn, const char* text, uint32_t color,
		lv_event_cb_t cb, int yOff)
		{
			btn = lv_button_create(parent);
			lv_obj_set_size(btn, 280, 60);
			lv_obj_set_style_radius(btn, 16, 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
			lv_obj_set_style_shadow_width(btn, 12, 0);
			lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
			lv_obj_set_style_border_width(btn, 0, 0);
			lv_obj_set_style_outline_width(btn, 4, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_color(btn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
			lv_obj_set_style_outline_opa(btn, LV_OPA_60, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_pad(btn, 3, LV_STATE_FOCUSED);
			lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, yOff);
			lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);

			auto lbl = lv_label_create(btn);
			lv_label_set_text(lbl, text);
			lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffffff), 0);
			lv_obj_set_style_text_font(lbl, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
			lv_obj_center(lbl);
			lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
		};

	mkModeBtn(m_btnClassic, "\u7ecf\u5178\u6a21\u5f0f  \u2022 3\u547d", BTN_GREEN, onClassicCb, 180);
	mkModeBtn(m_btnArcade, "\u8857\u673a\u6a21\u5f0f  \u2022 60\u79d2", BTN_ORANGE, onArcadeCb, 260);

	// ── 人数选择 ──
	auto playerTitle = lv_label_create(parent);
	lv_label_set_text(playerTitle, "\u73a9\u5bb6\u4eba\u6570");
	lv_obj_set_style_text_color(playerTitle, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(playerTitle, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_align(playerTitle, LV_ALIGN_TOP_MID, 0, 360);

	// 并排放置 1P / 2P 按钮
	auto playerRow = lv_obj_create(parent);
	lv_obj_set_size(playerRow, 300, 60);
	lv_obj_set_style_border_width(playerRow, 0, 0);
	lv_obj_set_style_bg_opa(playerRow, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_flow(playerRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_pad_column(playerRow, 16, 0);
	lv_obj_set_flex_align(playerRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_align(playerRow, LV_ALIGN_TOP_MID, 0, 400);

	m_btn1P = lv_button_create(playerRow);
	lv_obj_set_size(m_btn1P, 130, 56);
	lv_obj_set_style_radius(m_btn1P, 14, 0);
	lv_obj_set_style_bg_opa(m_btn1P, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_btn1P, 10, 0);
	lv_obj_set_style_shadow_opa(m_btn1P, LV_OPA_30, 0);
	lv_obj_set_style_border_width(m_btn1P, 0, 0);
	lv_obj_set_style_outline_width(m_btn1P, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_btn1P, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(m_btn1P, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_btn1P, 3, LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_btn1P, on1PCb, LV_EVENT_CLICKED, this);

	auto lbl1 = lv_label_create(m_btn1P);
	lv_label_set_text(lbl1, "1P");
	lv_obj_set_style_text_color(lbl1, lv_color_hex(0xffffffff), 0);
	lv_obj_center(lbl1);

	m_btn2P = lv_button_create(playerRow);
	lv_obj_set_size(m_btn2P, 130, 56);
	lv_obj_set_style_radius(m_btn2P, 14, 0);
	lv_obj_set_style_bg_opa(m_btn2P, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_btn2P, 10, 0);
	lv_obj_set_style_shadow_opa(m_btn2P, LV_OPA_30, 0);
	lv_obj_set_style_border_width(m_btn2P, 0, 0);
	lv_obj_set_style_outline_width(m_btn2P, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_btn2P, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(m_btn2P, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_btn2P, 3, LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_btn2P, on2PCb, LV_EVENT_CLICKED, this);

	auto lbl2 = lv_label_create(m_btn2P);
	lv_label_set_text(lbl2, "2P");
	lv_obj_set_style_text_color(lbl2, lv_color_hex(0xffffffff), 0);
	lv_obj_center(lbl2);

	updateBtnStyles();

	// ── 开始按钮 ──
	m_btnStart = GUI::createButton(parent, "\u5f00\u59cb\u6e38\u620f", 240, 60);
	lv_obj_set_style_radius(m_btnStart, 20, 0);
	lv_obj_set_style_bg_color(m_btnStart, lv_color_hex(BTN_GREEN), 0);
	lv_obj_set_style_bg_opa(m_btnStart, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_btnStart, 16, 0);
	lv_obj_set_style_shadow_color(m_btnStart, lv_color_hex(BTN_GREEN), 0);
	lv_obj_set_style_shadow_opa(m_btnStart, LV_OPA_40, 0);
	lv_obj_set_style_border_width(m_btnStart, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_btnStart, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_align(m_btnStart, LV_ALIGN_BOTTOM_MID, 0, -80);
	lv_obj_add_event_cb(m_btnStart, onStartCb, LV_EVENT_CLICKED, this);

	// ── 底部操作提示 ──
	m_hintLabel = lv_label_create(parent);
	lv_label_set_text(m_hintLabel,
		"\u2190\u2191\u2192\u2193 + A/B/X/Y \u5207\u6c34\u679c  |  \u89e6\u5c4f\u70b9\u6309\u5207\u7247");
	lv_obj_set_style_text_color(m_hintLabel, lv_color_hex(HINT), 0);
	lv_obj_set_style_text_font(m_hintLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(m_hintLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ============================================================
// 按钮样式更新
// ============================================================

void FruitNinjaRoom::updateBtnStyles()
{
	// 模式按钮高亮
	lv_obj_set_style_bg_color(m_btnClassic,
		m_classicSelected ? lv_color_hex(BTN_GREEN) : lv_color_hex(BTN_INACTIVE), 0);
	lv_obj_set_style_bg_color(m_btnArcade,
		!m_classicSelected ? lv_color_hex(BTN_ORANGE) : lv_color_hex(BTN_INACTIVE), 0);

	// 人数按钮高亮
	lv_obj_set_style_bg_color(m_btn1P,
		m_1PSelected ? lv_color_hex(BTN_ACTIVE) : lv_color_hex(BTN_INACTIVE), 0);
	lv_obj_set_style_bg_color(m_btn2P,
		!m_1PSelected ? lv_color_hex(BTN_BLUE) : lv_color_hex(BTN_INACTIVE), 0);
}

// ============================================================
// 焦点导航
// ============================================================

void FruitNinjaRoom::applyFocus()
{
	auto guard = display->lockGuard();
	auto clear = [](lv_obj_t* obj) { if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj) { if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clear(m_backBtn);
	clear(m_btnClassic);
	clear(m_btnArcade);
	clear(m_btn1P);
	clear(m_btn2P);
	clear(m_btnStart);

	switch (m_focusIdx)
	{
	case 0: focus(m_backBtn);     break;
	case 1: focus(m_btnClassic);  break;
	case 2: focus(m_btnArcade);   break;
	case 3: focus(m_btn1P);       break;
	case 4: focus(m_btn2P);       break;
	case 5: focus(m_btnStart);    break;
	}
}

void FruitNinjaRoom::activateFocus()
{
	switch (m_focusIdx)
	{
	case 0: lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr);    break;
	case 1: lv_obj_send_event(m_btnClassic, LV_EVENT_CLICKED, nullptr); break;
	case 2: lv_obj_send_event(m_btnArcade, LV_EVENT_CLICKED, nullptr);  break;
	case 3: lv_obj_send_event(m_btn1P, LV_EVENT_CLICKED, nullptr);      break;
	case 4: lv_obj_send_event(m_btn2P, LV_EVENT_CLICKED, nullptr);      break;
	case 5: lv_obj_send_event(m_btnStart, LV_EVENT_CLICKED, nullptr);   break;
	}
}

// ============================================================
// 手柄输入
// ============================================================

void FruitNinjaRoom::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 13;
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
		if (xTaskGetTickCount() < m_nextActionTime)
		{
			ESP_LOGI(TAG, "多次点击，已过滤");
			return;
		}
		m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;

		Task::addTask([](void* param) -> TickType_t {
			auto* room = static_cast<FruitNinjaRoom*>(param);
			if (room->getManager()) room->popApp();
			return Task::infinityTime;
			}, "fruitNinjaRoomBack", this, 0, Task::Affinity::None);
		return;
	}

	// A / L3：激活焦点
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		activateFocus();
		return;
	}

	// 摇杆归位判断
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[playerId] = 0;
		return;
	}
	if (xTaskGetTickCount() < m_nextMoveTime[playerId]) return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	if (lyUp && m_focusIdx > 0)   m_focusIdx--;
	if (lyDown && m_focusIdx < 5) m_focusIdx++;

	applyFocus();
}

void FruitNinjaRoom::onForeground()
{
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	applyFocus();
}

// ============================================================
// LVGL 按钮回调
// ============================================================

void FruitNinjaRoom::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤");
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_DELAY;

	Task::addTask([](void* param) -> TickType_t {
		auto* room = static_cast<FruitNinjaRoom*>(param);
		if (room->getManager()) room->popApp();
		return Task::infinityTime;
		}, "fruitNinjaRoomBack", self, 0, Task::Affinity::None);
}

void FruitNinjaRoom::onClassicCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaRoom*>(lv_event_get_user_data(e));
	self->m_classicSelected = true;
	self->updateBtnStyles();
}

void FruitNinjaRoom::onArcadeCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaRoom*>(lv_event_get_user_data(e));
	self->m_classicSelected = false;
	self->updateBtnStyles();
}

void FruitNinjaRoom::on1PCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaRoom*>(lv_event_get_user_data(e));
	self->m_1PSelected = true;
	self->updateBtnStyles();
}

void FruitNinjaRoom::on2PCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaRoom*>(lv_event_get_user_data(e));
	self->m_1PSelected = false;
	self->updateBtnStyles();
}

// 启动参数辅助结构
namespace
{
	struct StartParams
	{
		FruitNinjaRoom* room;
		FruitNinjaLogic::GameMode mode;
		int players;
	};
}

void FruitNinjaRoom::onStartCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaRoom*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤");
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_DELAY;

	auto mode = self->m_classicSelected
		? FruitNinjaLogic::GameMode::Classic
		: FruitNinjaLogic::GameMode::Arcade;
	int players = self->m_1PSelected ? 1 : 2;

	auto* params = new StartParams{ self, mode, players };

	Task::addTask([](void* p) -> TickType_t {
		auto* args = static_cast<StartParams*>(p);
		args->room->pushApp(new FruitNinjaApp(args->room->display, args->mode, args->players));
		delete args;
		return Task::infinityTime;
		}, "startFruitNinja", params, 0, Task::Affinity::None);
}
