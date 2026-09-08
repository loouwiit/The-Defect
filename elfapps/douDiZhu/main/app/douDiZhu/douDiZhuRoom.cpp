#include "douDiZhuRoom.hpp"
#include "douDiZhuApp.hpp"
#include "douDiZhuRemoteApp.hpp"
#include "gui/gui.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "host/hostManager.hpp"
#include "app/appStackManager.hpp"

#include <esp_log.h>

static constexpr uint32_t BG      = 0x0D0D1A;
static constexpr uint32_t TEXT    = 0xFFFFFF;
static constexpr uint32_t SUBTLE  = 0x888888;
static constexpr uint32_t CARD    = 0x1E1E2E;
static constexpr uint32_t ACCENT  = 0x4A6CF7;
static constexpr uint32_t SUCCESS = 0x44FF44;
static constexpr uint32_t WARN    = 0x888888;

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

DouDizhuRoom::DouDizhuRoom(Display* display)
	: App(display)
{
}

DouDizhuRoom::~DouDizhuRoom() = default;

// ════════════════════════════════════════════════════════════════
//  生命周期
// ════════════════════════════════════════════════════════════════

void DouDizhuRoom::init()
{
	App::init();

	// ── 从机模式：跳过本地 UI，直接进入远程游戏（由轮询 Task 执行，避免悬空指针） ──
	if (HostManager::instance().isClient()) {
		ESP_LOGI(TAG, "client mode, jump to remote game");
		m_pending = Pending::PushRemote;
	}

	auto guard = display->lockGuard();
	if (!guard) {
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}
	lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	buildUi();
	m_focusGroup = FOCUS_MODE;
	m_focusModeIdx = HostManager::instance().isClient() ? 0 : 1;   // 从机默认单机（无意义，会跳转）
	applyFocus();

	// 槽位状态定时刷新
	m_refreshTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DouDizhuRoom*>(lv_timer_get_user_data(t));
		self->refreshSlots();
	}, 1000, this);
	refreshSlots();

	// 轮询 Task：统一执行延迟动作（pushApp/popApp/startGame）
	m_roomTask = Task::addTask(roomTaskFn, "ddzRoom", this, 50, Task::Affinity::None);

	bgm = Audio::play("/root/system/music/douDiZhu/bgm.aac");
	bgm.setLoop(true);
	bgm.play();
}

void DouDizhuRoom::deinit()
{
	// 先停轮询 Task（返回 50ms 不会被 daemon 自删，removeTask 安全）
	if (m_roomTask) {
		Task::removeTask(m_roomTask);
		m_roomTask = nullptr;
	}
	// 删除定时器，防止对象销毁后 LVGL 回调访问悬空指针导致崩溃
	if (m_refreshTimer) {
		if (auto guard = display->lockGuard()) {
			lv_timer_delete(m_refreshTimer);
			m_refreshTimer = nullptr;
		}
	}
	App::deinit();
}

TickType_t DouDizhuRoom::roomTaskFn(void* param)
{
	static_cast<DouDizhuRoom*>(param)->roomTick();
	return 50;
}

void DouDizhuRoom::roomTick()
{
	if (m_pending == Pending::None) return;
	Pending action = m_pending;
	m_pending = Pending::None;
	switch (action) {
	case Pending::PushRemote:
		pushApp(new DouDizhuRemoteApp(display));
		break;
	case Pending::StartSingle:
	case Pending::StartOnline:
		startGame();
		break;
	case Pending::PopApp:
		popApp();
		break;
	default:
		break;
	}
}

void DouDizhuRoom::onForeground()
{
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
	m_prevButtons = 0xFFFF;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	m_starting = false;
	refreshSlots();
}

// ════════════════════════════════════════════════════════════════
//  UI 构建
// ════════════════════════════════════════════════════════════════

void DouDizhuRoom::buildUi()
{
	auto mainCol = lv_obj_create(screen);
	lv_obj_set_size(mainCol, lv_pct(100), lv_pct(100));
	lv_obj_set_style_border_width(mainCol, 0, 0);
	lv_obj_set_style_bg_opa(mainCol, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(mainCol, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(mainCol, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(mainCol, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(mainCol, 0, 0);

	// ── 顶部栏 ──
	auto topBar = lv_obj_create(mainCol);
	lv_obj_set_size(topBar, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(topBar, 0, 0);
	lv_obj_set_style_bg_opa(topBar, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(topBar, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(topBar, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(topBar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_left(topBar, 16, 0);
	lv_obj_set_style_pad_top(topBar, 8, 0);

	m_backBtn = GUI::createButton(topBar, "< 返回", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, lv_color_hex(0x303040), 0);
	lv_obj_set_style_border_width(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	auto spacer = lv_obj_create(topBar);
	lv_obj_set_size(spacer, 0, 0);
	lv_obj_set_flex_grow(spacer, 1);
	lv_obj_set_style_border_width(spacer, 0, 0);
	lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(spacer, LV_OBJ_FLAG_CLICKABLE);

	auto title = lv_label_create(topBar);
	lv_label_set_text(title, "斗地主");
	lv_obj_set_style_text_color(title, lv_color_hex(TEXT), 0);
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);

	auto spacerR = lv_obj_create(topBar);
	lv_obj_set_size(spacerR, 0, 0);
	lv_obj_set_flex_grow(spacerR, 1);
	lv_obj_set_style_border_width(spacerR, 0, 0);
	lv_obj_set_style_bg_opa(spacerR, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(spacerR, LV_OBJ_FLAG_CLICKABLE);

	// 不可见占位，与返回按钮等宽，使标题在屏幕居中
	auto rightDummy = lv_obj_create(topBar);
	lv_obj_set_size(rightDummy, 100, 44);
	lv_obj_set_style_border_width(rightDummy, 0, 0);
	lv_obj_set_style_bg_opa(rightDummy, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(rightDummy, LV_OBJ_FLAG_CLICKABLE);

	// ── 模式按钮行 ──
	auto btnRow = lv_obj_create(mainCol);
	lv_obj_set_size(btnRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(btnRow, 0, 0);
	lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(btnRow, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btnRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(btnRow, 12, 0);
	lv_obj_set_style_pad_top(btnRow, 20, 0);
	lv_obj_set_style_pad_bottom(btnRow, 20, 0);

	auto mkModeBtn = [&](lv_obj_t*& btn, const char* text, lv_event_cb_t cb) {
		btn = lv_button_create(btnRow);
		lv_obj_set_size(btn, 280, 56);
		lv_obj_set_style_radius(btn, 12, 0);
		lv_obj_set_style_bg_color(btn, lv_color_hex(CARD), 0);
		lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
		lv_obj_set_style_outline_width(btn, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_outline_color(btn, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_outline_pad(btn, 3, LV_STATE_FOCUSED);
		lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
		auto label = lv_label_create(btn);
		lv_label_set_text(label, text);
		lv_obj_center(label);
		lv_obj_set_style_text_color(label, lv_color_hex(TEXT), 0);
	};
	mkModeBtn(m_btnSingle, "单机（1人+AI）", onModeBtnCb);
	mkModeBtn(m_btnOnline, "联机（3台主机）", onModeBtnCb);

	// ── 槽位卡片行（slot 1/2） ──
	auto slotsRow = lv_obj_create(mainCol);
	lv_obj_set_size(slotsRow, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(slotsRow, 0, 0);
	lv_obj_set_style_bg_opa(slotsRow, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(slotsRow, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(slotsRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(slotsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_column(slotsRow, 12, 0);
	lv_obj_remove_flag(slotsRow, LV_OBJ_FLAG_SCROLLABLE);

	for (int i = 0; i < 2; i++) {
		auto card = lv_obj_create(slotsRow);
		lv_obj_set_size(card, 420, 180);
		lv_obj_set_style_radius(card, 12, 0);
		lv_obj_set_style_bg_color(card, lv_color_hex(CARD), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(card, 0, 0);
		lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_layout(card, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(card, 8, 0);
		m_slotCards[i] = card;

		char name[16];
		snprintf(name, sizeof(name), "从机 %d", i + 1);
		m_slotNames[i] = lv_label_create(card);
		lv_label_set_text(m_slotNames[i], name);
		lv_obj_set_style_text_color(m_slotNames[i], lv_color_hex(TEXT), 0);
		lv_obj_set_style_text_font(m_slotNames[i], FontLoader::getDefault(FontLoader::FontSize::Default), 0);

		auto statusRow = lv_obj_create(card);
		lv_obj_set_style_border_width(statusRow, 0, 0);
		lv_obj_set_style_bg_opa(statusRow, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(statusRow, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		// 纯布局容器：禁用滚动，防止状态文字溢出时出现滚动条
		lv_obj_remove_flag(statusRow, LV_OBJ_FLAG_SCROLLABLE);

		m_slotActivity[i] = lv_obj_create(statusRow);
		lv_obj_set_size(m_slotActivity[i], 10, 10);
		lv_obj_set_style_radius(m_slotActivity[i], LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_border_width(m_slotActivity[i], 0, 0);
		lv_obj_set_style_bg_color(m_slotActivity[i], lv_color_hex(WARN), 0);
		lv_obj_set_style_bg_opa(m_slotActivity[i], LV_OPA_COVER, 0);

		m_slotStatus[i] = lv_label_create(statusRow);
		lv_label_set_text(m_slotStatus[i], "未连接");
		lv_obj_set_style_text_color(m_slotStatus[i], lv_color_hex(SUBTLE), 0);
		lv_obj_set_style_text_font(m_slotStatus[i], FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	}

	// ── 提示 + 开始按钮 ──
	m_hintLbl = lv_label_create(mainCol);
	lv_label_set_text(m_hintLbl, "联机模式需 2 台从机已连接（多主机管理中配对）");
	lv_obj_set_style_text_color(m_hintLbl, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(m_hintLbl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(m_hintLbl, 16, 0);
	lv_obj_set_style_text_align(m_hintLbl, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(m_hintLbl, lv_pct(100));

	m_startBtn = GUI::createButton(mainCol, "开始游戏", 260, 60);
	lv_obj_set_style_margin_top(m_startBtn, 20, 0);
	lv_obj_set_style_bg_color(m_startBtn, lv_color_hex(ACCENT), 0);
	lv_obj_set_style_bg_opa(m_startBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_startBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_startBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_startBtn, onStartBtnCb, LV_EVENT_CLICKED, this);
}

void DouDizhuRoom::refreshSlots()
{
	if (!m_slotStatus[0]) return;
	const auto* slots = HostManager::instance().getSlots();
	for (int i = 0; i < 2; i++) {
		int slotId = i + 1;
		if (slots[slotId].connected) {
			char buf[96];
			snprintf(buf, sizeof(buf), "%.48s  (手柄x%d)", slots[slotId].hostname, slots[slotId].gamepadCount);
			lv_label_set_text(m_slotStatus[i], buf);
			lv_obj_set_style_text_color(m_slotStatus[i], lv_color_hex(SUCCESS), 0);
			lv_obj_set_style_bg_color(m_slotActivity[i], lv_color_hex(SUCCESS), 0);
		} else {
			lv_label_set_text(m_slotStatus[i], "未连接");
			lv_obj_set_style_text_color(m_slotStatus[i], lv_color_hex(SUBTLE), 0);
			lv_obj_set_style_bg_color(m_slotActivity[i], lv_color_hex(WARN), 0);
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  焦点导航
// ════════════════════════════════════════════════════════════════

void DouDizhuRoom::applyFocus()
{
	if (auto guard = display->lockGuard()) {
		auto focusStyle = [](lv_obj_t* obj, bool on) {
			if (on) {
				lv_obj_set_style_outline_width(obj, 3, 0);
				lv_obj_set_style_outline_color(obj, lv_color_white(), 0);
				lv_obj_set_style_outline_opa(obj, LV_OPA_80, 0);
			} else {
				lv_obj_set_style_outline_width(obj, 0, 0);
			}
		};
		focusStyle(m_backBtn, m_focusGroup == FOCUS_BACK);
		focusStyle(m_btnSingle, m_focusGroup == FOCUS_MODE && m_focusModeIdx == 0);
		focusStyle(m_btnOnline, m_focusGroup == FOCUS_MODE && m_focusModeIdx == 1);
		focusStyle(m_startBtn, m_focusGroup == FOCUS_BOTTOM && m_focusBottomIdx == 0);
	}
}

void DouDizhuRoom::activateFocus()
{
	switch (m_focusGroup) {
	case FOCUS_BACK:
		popApp();
		break;
	case FOCUS_MODE:
		// 模式选择后直接开始
		startGame();
		break;
	case FOCUS_BOTTOM:
		startGame();
		break;
	}
}

void DouDizhuRoom::startGame()
{
	if (m_starting) return;
	m_starting = true;

	if (m_focusModeIdx == 0) {
		ESP_LOGI(TAG, "start single mode");
		pushApp(new DouDizhuApp(display, DouDizhuApp::Mode::Single));
	} else {
		const auto* slots = HostManager::instance().getSlots();
		if (!slots[1].connected || !slots[2].connected) {
			if (auto guard = display->lockGuard())
				lv_label_set_text(m_hintLbl, "需要 2 台从机已连接才能联机");
			return;
		}
		ESP_LOGI(TAG, "start online (host) mode");
		pushApp(new DouDizhuApp(display, DouDizhuApp::Mode::Host));
	}
}

// ════════════════════════════════════════════════════════════════
//  手柄输入
// ════════════════════════════════════════════════════════════════

void DouDizhuRoom::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	if (hostId != 0 || playerId != 0) return;
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;
	TickType_t now = xTaskGetTickCount();

	// 左摇杆 + dpad 合并方向（参考 ChineseChess 死区）
	constexpr uint8_t deadZone = 13, center = 128;
	int8_t dx = 0, dy = 0;
	if (state.lx < center - deadZone || state.dpad == 0 || state.dpad == 6) dx = -1;
	else if (state.lx > center + deadZone || state.dpad == 2 || state.dpad == 4) dx = 1;
	if (state.ly < center - deadZone || state.dpad == 1 || state.dpad == 7) dy = -1;
	else if (state.ly > center + deadZone || state.dpad == 3 || state.dpad == 5) dy = 1;

	if ((dx != 0 || dy != 0)) {
		TickType_t delay = (m_nextMoveTime[0][0] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
		if (now < m_nextMoveTime[0][0]) return;
		m_nextMoveTime[0][0] = now + delay;

		if (m_focusGroup == FOCUS_MODE) {
			if (dx != 0) m_focusModeIdx = (m_focusModeIdx + dx + 2) % 2;
			else if (dy < 0) m_focusGroup = FOCUS_BACK;
			else if (dy > 0) m_focusGroup = FOCUS_BOTTOM;
		} else if (m_focusGroup == FOCUS_BACK) {
			if (dy > 0) m_focusGroup = FOCUS_MODE;
		} else if (m_focusGroup == FOCUS_BOTTOM) {
			if (dy < 0) m_focusGroup = FOCUS_MODE;
		}
		applyFocus();
	}

	bool confirm = (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
	               (newPress & static_cast<uint16_t>(GamepadButton::BTN_L3));
	if (confirm) {
		if (now >= m_nextActionTime) {
			m_nextActionTime = now + ACTION_DELAY;
			activateFocus();
		}
	}
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
		if (now >= m_nextActionTime) {
			m_nextActionTime = now + ACTION_DELAY;
			popApp();
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  静态回调
// ════════════════════════════════════════════════════════════════

void DouDizhuRoom::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuRoom*>(lv_event_get_user_data(e));
	if (!self) return;
	self->m_pending = Pending::PopApp;
}

void DouDizhuRoom::onModeBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuRoom*>(lv_event_get_user_data(e));
	if (!self) return;
	if (lv_event_get_current_target(e) == self->m_btnSingle) self->m_focusModeIdx = 0;
	else self->m_focusModeIdx = 1;
	self->applyFocus();
	self->m_pending = (self->m_focusModeIdx == 0) ? Pending::StartSingle : Pending::StartOnline;
}

void DouDizhuRoom::onStartBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DouDizhuRoom*>(lv_event_get_user_data(e));
	if (!self) return;
	self->m_pending = (self->m_focusModeIdx == 0) ? Pending::StartSingle : Pending::StartOnline;
}

// ════════════════════════════════════════════════════════════════
//  AI 能力
// ════════════════════════════════════════════════════════════════

std::string DouDizhuRoom::getAiContext()
{
	std::string ctx = "当前在斗地主大厅。\n"
		"可执行动作：\n"
		"- 单机模式：1 人 vs 2 AI\n"
		"- 联机模式：需要 2 台从机已连接\n";
	if (m_focusModeIdx == 0) ctx += "当前模式：单机\n";
	else ctx += "当前模式：联机\n";
	return ctx;
}

void DouDizhuRoom::onAiSelect(const AiSelectTarget& target)
{
	if (target.group == "mode") {
		m_focusModeIdx = (target.index == 0) ? 0 : 1;
		applyFocus();
	} else if (target.group == "action") {
		m_focusGroup = FOCUS_BOTTOM;
		applyFocus();
	}
}

void DouDizhuRoom::onAiActivate()
{
	activateFocus();
}
