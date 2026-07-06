#include "tetrisRoomApp.hpp"
#include "tetrisApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "app/bleSettingsApp/bleSettingsApp.hpp"
#include "bleGamepad/bleGamepad.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr uint32_t BG = 0x0D0D1A;
static constexpr uint32_t TEXT = 0xFFFFFF;
static constexpr uint32_t SUBTLE = 0x888888;
static constexpr uint32_t CARD = 0x1E1E2E;
static constexpr uint32_t ACCENT = 0x4A6CF7;
static constexpr uint32_t SUCCESS = 0x44FF44;
static constexpr uint32_t WARN = 0x888888;

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

TetrisRoomApp::TetrisRoomApp(Display* display)
	: App(display)
{
}

TetrisRoomApp::~TetrisRoomApp() = default;

// ════════════════════════════════════════════════════════════════
//  生命周期
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}

	lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	buildUi();

	// 初始聚焦
	m_focusGroup = FOCUS_PLAYERS;
	m_focusPlayersIdx = 1;  // 默认 2P
	applyFocus();
}

void TetrisRoomApp::onForeground()
{
	// 从 BleSettingsApp 回来，刷新手柄状态
	auto guard = display->lockGuard();
	refreshSlotStatus();

	// 防抖重置
	for (auto& t : m_nextMoveTime) t = 0;
	m_prevButtons = 0xFFFF;

	m_nextActionTime = xTaskGetTickCount() + 500;
}

// ════════════════════════════════════════════════════════════════
//  UI 构建
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::buildUi()
{
	// ── 主容器：flex 列，撑满屏幕 ──
	auto mainCol = lv_obj_create(screen);
	lv_obj_set_size(mainCol, lv_pct(100), lv_pct(100));
	lv_obj_set_style_border_width(mainCol, 0, 0);
	lv_obj_set_style_bg_opa(mainCol, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(mainCol, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(mainCol, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(mainCol, 0, 0);
	lv_obj_set_style_pad_top(mainCol, 8, 0);

	// ── 顶部栏（返回 + 标题 同一行） ──
	auto topBar = lv_obj_create(mainCol);
	lv_obj_set_size(topBar, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(topBar, 0, 0);
	lv_obj_set_style_bg_opa(topBar, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(topBar, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(topBar, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(topBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_left(topBar, 16, 0);
	lv_obj_set_style_pad_right(topBar, 16, 0);
	lv_obj_set_style_pad_top(topBar, 8, 0);

	m_backBtn = GUI::createButton(topBar, "< 返回", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, lv_color_hex(0x303040), 0);
	lv_obj_set_style_border_width(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	// 弹性分隔
	auto titleSpacer = lv_obj_create(topBar);
	lv_obj_set_size(titleSpacer, 0, 0);
	lv_obj_set_flex_grow(titleSpacer, 1);
	lv_obj_set_style_border_width(titleSpacer, 0, 0);
	lv_obj_set_style_bg_opa(titleSpacer, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(titleSpacer, LV_OBJ_FLAG_CLICKABLE);

	auto title = lv_label_create(topBar);
	lv_label_set_text(title, "俄罗斯方块");
	lv_obj_set_style_text_color(title, lv_color_hex(TEXT), 0);
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);

	// 右侧弹性分隔
	auto titleSpacerR = lv_obj_create(topBar);
	lv_obj_set_size(titleSpacerR, 0, 0);
	lv_obj_set_flex_grow(titleSpacerR, 1);
	lv_obj_set_style_border_width(titleSpacerR, 0, 0);
	lv_obj_set_style_bg_opa(titleSpacerR, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(titleSpacerR, LV_OBJ_FLAG_CLICKABLE);

	// 不可见占位，与返回按钮等宽，使标题在屏幕居中
	auto rightDummy = lv_obj_create(topBar);
	lv_obj_set_size(rightDummy, 100, 44);
	lv_obj_set_style_border_width(rightDummy, 0, 0);
	lv_obj_set_style_bg_opa(rightDummy, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(rightDummy, LV_OBJ_FLAG_CLICKABLE);

	// ── 副标题 ──
	auto sub = lv_label_create(mainCol);
	lv_label_set_text(sub, "选择玩家人数");
	lv_obj_set_style_text_color(sub, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(sub, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(sub, 16, 0);
	lv_obj_set_style_pad_bottom(sub, 16, 0);
	lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(sub, lv_pct(100));

	// ── 人数选择按钮行 ──
	auto btnRow = lv_obj_create(mainCol);
	lv_obj_set_size(btnRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(btnRow, 0, 0);
	lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(btnRow, 12, 0);
	lv_obj_set_style_pad_bottom(btnRow, 20, 0);

	auto mkBtn = [&](lv_obj_t*& btn, const char* text, lv_event_cb_t cb)
		{
			btn = lv_button_create(btnRow);
			lv_obj_set_size(btn, 150, 54);
			lv_obj_set_style_radius(btn, 12, 0);
			lv_obj_set_style_bg_color(btn, lv_color_hex(CARD), 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
			lv_obj_set_style_outline_width(btn, 3, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_color(btn, lv_color_white(), LV_STATE_FOCUSED);
			lv_obj_set_style_outline_pad(btn, 3, LV_STATE_FOCUSED);
			lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);

			auto label = lv_label_create(btn);
			lv_label_set_text(label, text);
			lv_obj_center(label);
			lv_obj_set_style_text_color(label, lv_color_hex(TEXT), 0);
		};

	mkBtn(m_btn1P, "单人游戏", onPlayerBtnCb);
	mkBtn(m_btn2P, "双人游戏", onPlayerBtnCb);
	mkBtn(m_btn3P, "三人游戏", onPlayerBtnCb);

	// ── 玩家槽位卡片行 ──
	auto slotsRow = lv_obj_create(mainCol);
	lv_obj_set_size(slotsRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(slotsRow, 0, 0);
	lv_obj_set_style_bg_opa(slotsRow, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(slotsRow, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(slotsRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(slotsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_left(slotsRow, 24, 0);
	lv_obj_set_style_pad_right(slotsRow, 24, 0);
	lv_obj_set_style_pad_column(slotsRow, 12, 0);

	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		auto card = lv_obj_create(slotsRow);
		lv_obj_set_size(card, 400, 200);
		lv_obj_set_style_radius(card, 12, 0);
		lv_obj_set_style_bg_color(card, lv_color_hex(CARD), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(card, 0, 0);
		lv_obj_set_layout(card, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(card, 8, 0);
		m_slotCards[i] = card;

		// 第一行：玩家名
		char name[16];
		snprintf(name, sizeof(name), "玩家%d", i + 1);
		m_slotNames[i] = lv_label_create(card);
		lv_label_set_text(m_slotNames[i], name);
		lv_obj_set_style_text_color(m_slotNames[i], lv_color_hex(TEXT), 0);
		lv_obj_set_style_text_font(m_slotNames[i],
			FontLoader::getDefault(FontLoader::FontSize::Default), 0);

		// 第二行：活动指示 + 状态文字
		auto statusRow = lv_obj_create(card);
		lv_obj_set_style_border_width(statusRow, 0, 0);
		lv_obj_set_style_bg_opa(statusRow, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(statusRow, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(statusRow, 0, 0);
		lv_obj_set_size(statusRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

		// 活动指示点（根据连接状态显示不同颜色的圆点）
		m_slotActivity[i] = lv_obj_create(statusRow);
		lv_obj_set_size(m_slotActivity[i], 10, 10);
		lv_obj_set_style_radius(m_slotActivity[i], LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_border_width(m_slotActivity[i], 0, 0);
		lv_obj_set_style_bg_color(m_slotActivity[i], lv_color_hex(WARN), 0);
		lv_obj_set_style_bg_opa(m_slotActivity[i], LV_OPA_COVER, 0);
		lv_obj_set_style_pad_right(m_slotActivity[i], 8, 0);

		m_slotStatus[i] = lv_label_create(statusRow);
		lv_label_set_text(m_slotStatus[i], "等待中");
		lv_obj_set_style_text_color(m_slotStatus[i], lv_color_hex(SUBTLE), 0);
		lv_obj_set_style_text_font(m_slotStatus[i],
			FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	}
	refreshSlotStatus();

	// 初始化槽位可见性（默认 2P：隐藏 P3）
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i < m_selectedPlayers)
			lv_obj_clear_flag(m_slotCards[i], LV_OBJ_FLAG_HIDDEN);
		else
			lv_obj_add_flag(m_slotCards[i], LV_OBJ_FLAG_HIDDEN);
	}

	// ── 弹性分隔（把底部按钮推到最下） ──
	auto spacer2 = lv_obj_create(mainCol);
	lv_obj_set_size(spacer2, lv_pct(100), 0);
	lv_obj_set_flex_grow(spacer2, 1);
	lv_obj_set_style_border_width(spacer2, 0, 0);
	lv_obj_set_style_bg_opa(spacer2, LV_OPA_TRANSP, 0);

	// ── 底部按钮行 ──
	auto bottomRow = lv_obj_create(mainCol);
	lv_obj_set_size(bottomRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(bottomRow, 0, 0);
	lv_obj_set_style_bg_opa(bottomRow, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(bottomRow, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(bottomRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(bottomRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(bottomRow, 12, 0);
	lv_obj_set_style_pad_bottom(bottomRow, 24, 0);

	m_btnSettings = GUI::createButton(bottomRow, "手柄配对", 140, 50);
	lv_obj_set_style_radius(m_btnSettings, 12, 0);
	lv_obj_set_style_bg_color(m_btnSettings, lv_color_hex(CARD), 0);
	lv_obj_set_style_outline_width(m_btnSettings, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_btnSettings, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_btnSettings, 3, LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_btnSettings, onSettingsBtnCb, LV_EVENT_CLICKED, this);

	m_btnStart = GUI::createButton(bottomRow, "开始游戏", 140, 50);
	lv_obj_set_style_radius(m_btnStart, 12, 0);
	lv_obj_set_style_bg_color(m_btnStart, lv_color_hex(ACCENT), 0);
	lv_obj_set_style_outline_width(m_btnStart, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_btnStart, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_btnStart, 3, LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_btnStart, onStartBtnCb, LV_EVENT_CLICKED, this);
}

// ════════════════════════════════════════════════════════════════
//  状态刷新
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::refreshSlotStatus()
{
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		auto* ctx = BleGamepad::instance().getDevice(i);
		bool connected = ctx && ctx->connected;
		if (connected)
		{
			char buf[56];
			snprintf(buf, sizeof(buf), "%s", ctx->name);
			lv_label_set_text(m_slotStatus[i], buf);
			lv_obj_set_style_text_color(m_slotStatus[i], lv_color_hex(SUCCESS), 0);
			lv_obj_set_style_bg_color(m_slotActivity[i], lv_color_hex(SUCCESS), 0);
		}
		else
		{
			lv_label_set_text(m_slotStatus[i], "等待手柄连接/触摸控制");
			lv_obj_set_style_text_color(m_slotStatus[i], lv_color_hex(SUBTLE), 0);
			lv_obj_set_style_bg_color(m_slotActivity[i], lv_color_hex(WARN), 0);
		}
	}
}

void TetrisRoomApp::updateActivity()
{
	TickType_t now = xTaskGetTickCount();
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		bool active = (now - m_lastActivity[i] < ACTIVITY_TIMEOUT);
		if (active != m_lastActivityState[i])
		{
			m_lastActivityState[i] = active;
			lv_obj_set_style_bg_opa(m_slotActivity[i],
				active ? LV_OPA_COVER : LV_OPA_30, 0);
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  焦点系统
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::applyFocus()
{
	auto guard = display->lockGuard();
	auto clear = [](lv_obj_t* obj)
		{ if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clear(m_backBtn);
	clear(m_btn1P);
	clear(m_btn2P);
	clear(m_btn3P);
	clear(m_btnSettings);
	clear(m_btnStart);

	switch (m_focusGroup)
	{
	case FOCUS_BACK:
		focus(m_backBtn);
		break;
	case FOCUS_PLAYERS:
	{
		lv_obj_t* btns[] =
		{ m_btn1P, m_btn2P, m_btn3P };
		if (m_focusPlayersIdx >= 0 && m_focusPlayersIdx < 3)
			focus(btns[m_focusPlayersIdx]);
		break;
	}
	case FOCUS_BOTTOM:
	{
		lv_obj_t* btns[] =
		{ m_btnSettings, m_btnStart };
		if (m_focusBottomIdx >= 0 && m_focusBottomIdx < 2)
			focus(btns[m_focusBottomIdx]);
		break;
	}
	}
}

void TetrisRoomApp::activateFocus()
{
	auto guard = display->lockGuard();
	switch (m_focusGroup)
	{
	case FOCUS_BACK:
		lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr);
		break;
	case FOCUS_PLAYERS:
	{
		lv_obj_t* btns[] =
		{ m_btn1P, m_btn2P, m_btn3P };
		if (m_focusPlayersIdx >= 0 && m_focusPlayersIdx < 3)
			lv_obj_send_event(btns[m_focusPlayersIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_BOTTOM:
	{
		lv_obj_t* btns[] =
		{ m_btnSettings, m_btnStart };
		if (m_focusBottomIdx >= 0 && m_focusBottomIdx < 2)
			lv_obj_send_event(btns[m_focusBottomIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	}
}

// ════════════════════════════════════════════════════════════════
//  操作
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::startGame()
{
	if (xTaskGetTickCount() < m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", m_nextActionTime - xTaskGetTickCount());
		return;
	}
	m_nextActionTime = xTaskGetTickCount() + 500;

	ESP_LOGI(TAG, "开始游戏: %d 人", m_selectedPlayers);

	if (!m_manager)
	{
		ESP_LOGE(TAG, "startGame: no manager");
		return;
	}

	// push TetrisApp 到当前栈（不创建新栈，游戏结束后回到大厅）
	m_manager->push(new TetrisApp(display, m_selectedPlayers));
}

// ════════════════════════════════════════════════════════════════
//  BLE 手柄输入
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	// 记录活动
	if (playerId < MAX_PLAYERS)
	{
		m_lastActivity[playerId] = xTaskGetTickCount();
		{
			auto guard = display->lockGuard();
			updateActivity();
		}
	}

	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// B → 返回
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		Task::addTask([](void* param) -> TickType_t
			{
				auto* room = static_cast<TetrisRoomApp*>(param);
				if (room->getManager()) room->popApp();
				return Task::infinityTime;
			}, "tetrisRoomBack", this, 0, Task::Affinity::None);
		return;
	}

	// A / L3 → 激活聚焦
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		activateFocus();
		return;
	}

	// 摇杆归位
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[playerId] = 0;
		return;
	}
	if (m_nextMoveTime[playerId] > xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	// ── 二维焦点组导航 ──
	switch (m_focusGroup)
	{
	case FOCUS_BACK:
		if (lyDown) m_focusGroup = FOCUS_PLAYERS;
		break;

	case FOCUS_PLAYERS:
		if (lxLeft && m_focusPlayersIdx > 0)     m_focusPlayersIdx--;
		if (lxRight && m_focusPlayersIdx < 2)     m_focusPlayersIdx++;
		if (lyUp)    m_focusGroup = FOCUS_BACK;
		if (lyDown)  m_focusGroup = FOCUS_BOTTOM;
		break;

	case FOCUS_BOTTOM:
		if (lxLeft && m_focusBottomIdx > 0)   m_focusBottomIdx--;
		if (lxRight && m_focusBottomIdx < 1)   m_focusBottomIdx++;
		if (lyUp)    m_focusGroup = FOCUS_PLAYERS;
		break;
	}

	applyFocus();
}

// ════════════════════════════════════════════════════════════════
//  LVGL 回调
// ════════════════════════════════════════════════════════════════

void TetrisRoomApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<TetrisRoomApp*>(lv_event_get_user_data(e));
	Task::addTask([](void* param) -> TickType_t
		{
			auto* room = static_cast<TetrisRoomApp*>(param);
			if (room->getManager()) room->popApp();
			return Task::infinityTime;
		}, "tetrisRoomBack", self, 0, Task::Affinity::None);
}

void TetrisRoomApp::onPlayerBtnCb(lv_event_t* e)
{
	auto* self = static_cast<TetrisRoomApp*>(lv_event_get_user_data(e));
	auto* btn = lv_event_get_target(e);

	if (btn == self->m_btn1P) self->m_selectedPlayers = 1;
	if (btn == self->m_btn2P) self->m_selectedPlayers = 2;
	if (btn == self->m_btn3P) self->m_selectedPlayers = 3;

	ESP_LOGI(TAG, "选择人数: %d", self->m_selectedPlayers);

	// 动态显示/隐藏玩家槽位
	for (int i = 0; i < MAX_PLAYERS; i++)
	{
		if (i < self->m_selectedPlayers)
		{
			lv_obj_clear_flag(self->m_slotCards[i], LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(self->m_slotCards[i], LV_OBJ_FLAG_HIDDEN);
		}
	}

	// 更新聚焦到对应人数按钮
	self->m_focusGroup = FOCUS_PLAYERS;
	self->m_focusPlayersIdx = self->m_selectedPlayers - 1;
	self->applyFocus();
}

void TetrisRoomApp::onSettingsBtnCb(lv_event_t* e)
{
	auto* self = static_cast<TetrisRoomApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	Task::addTask([](void* param) -> TickType_t
		{
			auto* room = static_cast<TetrisRoomApp*>(param);
			if (room->getManager())
				room->getManager()->push(new BleSettingsApp(room->getDisplay()));
			return Task::infinityTime;
		}, "tetrisRoomSettings", self, 0, Task::Affinity::None);
}

void TetrisRoomApp::onStartBtnCb(lv_event_t* e)
{
	auto* self = static_cast<TetrisRoomApp*>(lv_event_get_user_data(e));
	Task::addTask([](void* param) -> TickType_t
		{
			auto* room = static_cast<TetrisRoomApp*>(param);
			room->startGame();
			return Task::infinityTime;
		}, "tetrisRoomStart", self, 0, Task::Affinity::None);
}
