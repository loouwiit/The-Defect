#include "consoleManagerApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "host/hostManager.hpp"
#include "discovery/discovery.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "wifi/mdns.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <utility>

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

ConsoleManagerApp::ConsoleManagerApp(Display* display)
	: App(display)
{
	for (int h = 0; h < 4; h++)
		for (int p = 0; p < 4; p++)
			m_maskState[h][p] = (h == 0);  // 默认仅本机 P0-P3 启用
}

ConsoleManagerApp::~ConsoleManagerApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}

	lv_obj_set_style_bg_color(screen, GUI::Color::BG, 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// 构建双视图
	buildHostView();
	buildClientView();
	applyViewMode();

	// 初始聚焦
	m_focusGroup = FOCUS_TITLE;
	m_focusTitleIdx = 0;
	m_focusListIdx = 0;
	m_focusSlotIdx = 0;
	applyFocus();

	// 注册槽位更新回调
	HostManager::instance().setOnSlotUpdate(
		[this](const HostManager::ClientSlot* slots) {
			// 在 LVGL task 上下文中更新 UI
			Task::addTask([](void* param) -> TickType_t {
				auto* app = static_cast<ConsoleManagerApp*>(param);
				if (app->running) app->updateSlotStatus();
				return Task::infinityTime;
				}, "slotUpdate", this, 0, Task::Affinity::None);
		});

	// 注册角色变化回调
	HostManager::instance().setOnRoleChange(
		[this](bool) {
			Task::addTask([](void* param) -> TickType_t {
				auto* app = static_cast<ConsoleManagerApp*>(param);
				if (app->running) app->applyViewMode();
				return Task::infinityTime;
				}, "roleChange", this, 0, Task::Affinity::None);
		});

	// 定时器
	m_refreshTimer = lv_timer_create(timerCb, RefreshInterval, this);

	ESP_LOGI(TAG, "多主机管理 App 初始化完成");
}

void ConsoleManagerApp::deinit()
{
	// 清除 HostManager 回调（防止二次进入时旧回调访问野指针）
	HostManager::instance().setOnSlotUpdate(nullptr);
	HostManager::instance().setOnRoleChange(nullptr);

	if (m_refreshTimer)
	{
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}

	// 清零槽位控件指针（下一个实例会重新创建）
	for (auto& w : m_slotsUI)
		w = SlotWidgets{};

	running = false;
	deletable = !m_scanActive;

	if (m_scanActive)
		ESP_LOGI(TAG, "扫描进行中，延迟销毁");
	else
		ESP_LOGI(TAG, "已释放");
}

void ConsoleManagerApp::onForeground()
{
	auto guard = display->lockGuard();
	if (m_refreshTimer)
		lv_timer_resume(m_refreshTimer);
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
	m_nextActionTime = xTaskGetTickCount() + 500;
	updateSlotStatus();
	ESP_LOGI(TAG, "前台");
}

void ConsoleManagerApp::onBackground()
{
	auto guard = display->lockGuard();
	if (m_refreshTimer)
		lv_timer_pause(m_refreshTimer);
	ESP_LOGI(TAG, "后台");
}

// ════════════════════════════════════════════════════════════════
// 视图构建
// ════════════════════════════════════════════════════════════════

/// 创建主机视图（扫描列表 + 4 槽位）
void ConsoleManagerApp::buildHostView()
{
	m_hostView = lv_obj_create(screen);
	lv_obj_set_size(m_hostView, lv_pct(100), lv_pct(100));
	lv_obj_set_style_border_width(m_hostView, 0, 0);
	lv_obj_set_style_bg_opa(m_hostView, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(m_hostView, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_hostView, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(m_hostView, 0, 0);

	// ── 顶部栏 ──
	auto top_bar = GUI::createFlex(m_hostView, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(top_bar, 8, 0);
	lv_obj_set_style_pad_left(top_bar, 16, 0);
	lv_obj_set_style_pad_right(top_bar, 16, 0);
	lv_obj_set_style_border_width(top_bar, 0, 0);
	lv_obj_set_style_bg_opa(top_bar, LV_OPA_TRANSP, 0);

	m_backBtn = GUI::createButton(top_bar, "返回", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	auto title = GUI::createTitle(top_bar, "多主机管理");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	m_scanBtn = GUI::createButton(top_bar, "扫描", 100, 40);
	m_scanBtnLabel = lv_obj_get_child(m_scanBtn, 0);
	lv_obj_set_style_border_width(m_scanBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_scanBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_scanBtn, onScanBtnCb, LV_EVENT_CLICKED, this);

	// ── 扫描列表 ──
	auto list_section = GUI::createFlex(m_hostView, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(list_section, 1);
	lv_obj_set_style_pad_all(list_section, 4, 0);
	lv_obj_set_style_pad_left(list_section, 16, 0);
	lv_obj_set_style_pad_right(list_section, 16, 0);
	lv_obj_set_style_border_width(list_section, 0, 0);
	lv_obj_set_style_bg_opa(list_section, LV_OPA_TRANSP, 0);

	auto list_title = GUI::createSubtitle(list_section, "发现的主机");
	lv_obj_set_style_text_font(list_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_bottom(list_title, 4, 0);

	m_scanListContainer = lv_obj_create(list_section);
	lv_obj_set_width(m_scanListContainer, lv_pct(100));
	lv_obj_set_flex_grow(m_scanListContainer, 1);
	lv_obj_set_style_border_width(m_scanListContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_scanListContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_scanListContainer, 0, 0);
	lv_obj_set_layout(m_scanListContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_scanListContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollbar_mode(m_scanListContainer, LV_SCROLLBAR_MODE_AUTO);

	auto empty_hint = GUI::createLabel(m_scanListContainer, "点击「扫描」发现局域网内的其他主机");
	lv_obj_set_style_text_color(empty_hint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(empty_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(empty_hint, 12, 0);
	lv_obj_set_width(empty_hint, lv_pct(100));
	lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
	m_scanRows.push_back(empty_hint);

	// ── 4 槽位 ──
	auto slots_label = GUI::createSubtitle(m_hostView, "连接设备");
	lv_obj_set_style_text_font(slots_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_left(slots_label, 16, 0);
	lv_obj_set_style_pad_bottom(slots_label, 4, 0);

	buildSlotCards();
}

/// 创建每槽卡片（4 个）
void ConsoleManagerApp::buildSlotCards()
{
	auto slot_row = lv_obj_create(m_hostView);
	lv_obj_set_width(slot_row, lv_pct(100));
	lv_obj_set_height(slot_row, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(slot_row, 0, 0);
	lv_obj_set_style_bg_opa(slot_row, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(slot_row, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(slot_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(slot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_left(slot_row, 8, 0);
	lv_obj_set_style_pad_right(slot_row, 8, 0);
	lv_obj_set_style_pad_column(slot_row, 6, 0);
	lv_obj_set_style_pad_bottom(slot_row, 8, 0);

	for (int s = 0; s < 4; s++)
	{
		auto& w = m_slotsUI[s];
		auto card = lv_obj_create(slot_row);
		w.card = card;
		lv_obj_set_flex_grow(card, 1);
		lv_obj_set_height(card, SlotHight);
		styleCard(card);
		lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(card, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_layout(card, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(card, 4, 0);
		lv_obj_set_style_pad_top(card, 6, 0);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);

		// Slot 名称（单行: "SlotN: hostname"）
		auto name_flex = lv_obj_create(card);
		lv_obj_set_width(name_flex, lv_pct(100));
		lv_obj_set_flex_grow(name_flex, 1);
		lv_obj_set_style_border_width(name_flex, 0, 0);
		lv_obj_set_style_bg_opa(name_flex, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(name_flex, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(name_flex, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(name_flex, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(name_flex, 0, 0);

		w.nameLabel = lv_label_create(name_flex);
		char nameBuf[64];
		snprintf(nameBuf, sizeof(nameBuf), "Slot%d: %s", s,
			(s == 0) ? mdnsGetConfiguredHostname() : "--");
		lv_label_set_text(w.nameLabel, nameBuf);
		lv_obj_set_style_text_color(w.nameLabel, GUI::Color::TEXT, 0);
		lv_obj_set_style_text_font(w.nameLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_label_set_long_mode(w.nameLabel, LV_LABEL_LONG_DOT);
		lv_obj_set_flex_grow(w.nameLabel, 1);
		lv_obj_set_style_text_align(w.nameLabel, LV_TEXT_ALIGN_CENTER, 0);

		// 掩码按钮行
		auto mask_row = lv_obj_create(card);
		lv_obj_set_size(mask_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(mask_row, 0, 0);
		lv_obj_set_style_bg_opa(mask_row, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(mask_row, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(mask_row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(mask_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_column(mask_row, 4, 0);
		lv_obj_set_style_pad_top(mask_row, 4, 0);
		lv_obj_set_style_pad_bottom(mask_row, 4, 0);

		for (int p = 0; p < 4; p++)
		{
			auto btn = lv_btn_create(mask_row);
			w.maskBtns[p] = btn;
			lv_obj_set_size(btn, MASK_BTN_SIZE, MASK_BTN_SIZE);
			lv_obj_set_style_radius(btn, 4, 0);
			lv_obj_set_style_border_width(btn, 0, 0);
			lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_color(btn, lv_color_white(), LV_STATE_FOCUSED);
			lv_obj_set_style_outline_pad(btn, 2, LV_STATE_FOCUSED);

			auto lbl = lv_label_create(btn);
			char txt[4];
			snprintf(txt, sizeof(txt), "P%d", p);
			lv_label_set_text(lbl, txt);
			lv_obj_center(lbl);
			lv_obj_set_style_text_font(lbl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

			uint32_t idx = (uint32_t)(s * 4 + p);
			lv_obj_set_user_data(btn, (void*)(uintptr_t)idx);
			lv_obj_add_event_cb(btn, onMaskBtnCb, LV_EVENT_CLICKED, this);
		}

		// 操作按钮行（Slot 0 用占位按钮保持对齐）
		auto action_row = lv_obj_create(card);
		lv_obj_set_size(action_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(action_row, 0, 0);
		lv_obj_set_style_bg_opa(action_row, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(action_row, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(action_row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(action_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_column(action_row, 4, 0);
		lv_obj_set_style_pad_top(action_row, 2, 0);

		if (s > 0)
		{
			// 交换按钮
			w.swapBtn = GUI::createButton(action_row, "交换", 62, 28);
			lv_obj_set_style_radius(w.swapBtn, 6, 0);
			lv_obj_set_style_bg_color(w.swapBtn, LV_COLOR_MAKE(0x40, 0x60, 0x90), 0);
			lv_obj_set_style_border_width(w.swapBtn, 2, LV_STATE_FOCUSED);
			lv_obj_set_style_border_color(w.swapBtn, lv_color_white(), LV_STATE_FOCUSED);
			lv_obj_set_user_data(w.swapBtn, (void*)(uintptr_t)s);
			lv_obj_add_event_cb(w.swapBtn, onSwapBtnCb, LV_EVENT_CLICKED, this);

			// 断开按钮
			w.disconnectBtn = GUI::createButton(action_row, "断开", 62, 28);
			lv_obj_set_style_radius(w.disconnectBtn, 6, 0);
			lv_obj_set_style_bg_color(w.disconnectBtn, LV_COLOR_MAKE(0x80, 0x30, 0x30), 0);
			lv_obj_set_style_border_width(w.disconnectBtn, 2, LV_STATE_FOCUSED);
			lv_obj_set_style_border_color(w.disconnectBtn, lv_color_white(), LV_STATE_FOCUSED);
			lv_obj_set_user_data(w.disconnectBtn, (void*)(uintptr_t)s);
			lv_obj_add_event_cb(w.disconnectBtn, onDisconnectBtnCb, LV_EVENT_CLICKED, this);
		}
		else
		{
			// Slot 0 占位按钮（维持与其他槽位相同高度）
			auto ph1 = lv_obj_create(action_row);
			lv_obj_set_size(ph1, 62, 28);
			lv_obj_set_style_border_width(ph1, 0, 0);
			lv_obj_set_style_bg_opa(ph1, LV_OPA_TRANSP, 0);
			auto ph2 = lv_obj_create(action_row);
			lv_obj_set_size(ph2, 62, 28);
			lv_obj_set_style_border_width(ph2, 0, 0);
			lv_obj_set_style_bg_opa(ph2, LV_OPA_TRANSP, 0);
		}

	}
}

/// 创建从机视图（已连接信息 + 断开按钮）
void ConsoleManagerApp::buildClientView()
{
	m_clientView = lv_obj_create(screen);
	lv_obj_set_size(m_clientView, lv_pct(100), lv_pct(100));
	lv_obj_set_style_border_width(m_clientView, 0, 0);
	lv_obj_set_style_bg_opa(m_clientView, LV_OPA_TRANSP, 0);
	lv_obj_add_flag(m_clientView, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_layout(m_clientView, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_clientView, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(m_clientView, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(m_clientView, 0, 0);

	auto icon = lv_label_create(m_clientView);
	lv_label_set_text(icon, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_color(icon, GUI::Color::SUCCESS, 0);
	lv_obj_set_style_text_font(icon, FontLoader::getDefault(FontLoader::FontSize::Large), 0);

	m_connectedInfoLabel = lv_label_create(m_clientView);
	lv_label_set_text(m_connectedInfoLabel, "已连接到");
	lv_obj_set_style_text_color(m_connectedInfoLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_connectedInfoLabel, FontLoader::getDefault(), 0);
	lv_obj_set_style_pad_top(m_connectedInfoLabel, 12, 0);

	m_disconnectBtn = GUI::createButton(m_clientView, "断开连接", 160, 48);
	lv_obj_set_style_bg_color(m_disconnectBtn, LV_COLOR_MAKE(0x80, 0x30, 0x30), 0);
	lv_obj_set_style_radius(m_disconnectBtn, 12, 0);
	lv_obj_set_style_pad_top(m_disconnectBtn, 24, 0);
	lv_obj_add_event_cb(m_disconnectBtn, onDisconnectBtnCb, LV_EVENT_CLICKED, this);
}

/// 根据角色切换主机/从机视图
void ConsoleManagerApp::applyViewMode()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	if (HostManager::instance().isClient())
	{
		lv_obj_add_flag(m_hostView, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(m_clientView, LV_OBJ_FLAG_HIDDEN);

		char buf[96];
		snprintf(buf, sizeof(buf), "已连接到 %s (Slot %d)",
			HostManager::instance().getRemoteHostname(),
			HostManager::instance().getMySlot());
		lv_label_set_text(m_connectedInfoLabel, buf);
	}
	else
	{
		lv_obj_clear_flag(m_hostView, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_clientView, LV_OBJ_FLAG_HIDDEN);
	}
}

// ════════════════════════════════════════════════════════════════
// 定时器
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::timerCb(lv_timer_t* t)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_timer_get_user_data(t));
	self->refreshUi();
}

void ConsoleManagerApp::refreshUi()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	// 扫描状态更新
	if (m_scanActive)
	{
		updateScanList();
		if (!m_scanActive)
			lv_label_set_text(m_scanBtnLabel, "扫描");
	}

	// 槽位同步（从 HostManager 读取最新）
	updateSlotStatus();
}

// ════════════════════════════════════════════════════════════════
// 扫描
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::updateScanList()
{
	auto guard = display->lockGuard();
	for (auto* row : m_scanRows)
		if (row) lv_obj_del(row);
	m_scanRows.clear();
	m_connectBtns.clear();

	if (m_consoles.empty())
	{
		auto hint = GUI::createLabel(m_scanListContainer,
			m_scanActive ? "正在搜索..." : "未发现其他主机");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_pad_top(hint, 12, 0);
		lv_obj_set_width(hint, lv_pct(100));
		lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
		m_scanRows.push_back(hint);
		return;
	}

	for (size_t i = 0; i < m_consoles.size(); i++)
	{
		const auto& c = m_consoles[i];
		auto row = lv_obj_create(m_scanListContainer);
		lv_obj_set_width(row, lv_pct(100));
		lv_obj_set_height(row, RowHight);
		styleCard(row);
		lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_border_width(row, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(row, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_layout(row, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(row, 6, 0);
		lv_obj_set_style_pad_left(row, 12, 0);
		lv_obj_set_style_pad_right(row, 8, 0);
		lv_obj_set_user_data(row, (void*)(uintptr_t)i);

		auto left_flex = lv_obj_create(row);
		lv_obj_set_height(left_flex, lv_pct(100));
		lv_obj_set_flex_grow(left_flex, 1);
		lv_obj_remove_flag(left_flex, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
		lv_obj_set_style_border_width(left_flex, 0, 0);
		lv_obj_set_style_bg_opa(left_flex, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(left_flex, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(left_flex, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_style_pad_all(left_flex, 0, 0);

		auto name_label = GUI::createLabel(left_flex, c.hostname);
		lv_obj_set_style_text_font(name_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

		char ipStr[32];
		snprintf(ipStr, sizeof(ipStr), "%s", c.ip);
		auto ip_label = GUI::createLabel(left_flex, ipStr);
		lv_obj_set_style_text_font(ip_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(ip_label, GUI::Color::SUBTLE, 0);

		auto connectBtn = GUI::createButton(row, "连接", 72, 32);
		lv_obj_set_style_radius(connectBtn, 6, 0);
		lv_obj_set_style_bg_color(connectBtn, LV_COLOR_MAKE(0x00, 0x77, 0x00), 0);
		lv_obj_set_user_data(connectBtn, (void*)(uintptr_t)i);
		lv_obj_add_event_cb(connectBtn, onConnectBtnCb, LV_EVENT_CLICKED, this);

		m_scanRows.push_back(row);
		m_connectBtns.push_back(connectBtn);
	}
}

void ConsoleManagerApp::doScan()
{
	if (!running)
	{
		if (!deletable) { deletable = true; }
		return;
	}
	if (m_scanActive) return;

	m_scanActive = true;
	{
		auto guard = display->lockGuard();
		lv_label_set_text(m_scanBtnLabel, "搜索中...");
	}

	DiscoveredConsole buffer[MaxConsoles];
	int count = discoverConsoles(buffer, MaxConsoles, ScanTimeoutMs);

	m_consoles.clear();
	for (int i = 0; i < count; i++)
		m_consoles.push_back(buffer[i]);

	m_scanActive = false;

	if (!running)
	{
		deletable = true;
		return;
	}

	{
		auto guard = display->lockGuard();
		lv_label_set_text(m_scanBtnLabel, "扫描");
		updateScanList();
	}
	ESP_LOGI(TAG, "扫描完成，发现 %zu 台主机", m_consoles.size());
}

void ConsoleManagerApp::doConnect(size_t index)
{
	if (index >= m_consoles.size())
	{
		ESP_LOGE(TAG, "无效索引 %zu", index);
		return;
	}

	const auto& c = m_consoles[index];
	ESP_LOGI(TAG, "连接到主机: %s @ %s:%u", c.hostname, c.ip, c.port);

	if (HostManager::instance().isClient())
		HostManager::instance().disconnectFromHost();

	HostManager::instance().connectToHost(c.ip, 8080, c.hostname);
}

// ════════════════════════════════════════════════════════════════
// 槽位更新
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::updateSlotStatus()
{
	auto guard = display->lockGuard();
	const auto* slots = HostManager::instance().getSlots();
	for (int i = 0; i < 4; i++)
		updateSlotCard(i, slots[i]);
}

void ConsoleManagerApp::updateSlotCard(int slot, const HostManager::ClientSlot& info)
{
	auto& w = m_slotsUI[slot];
	if (!w.card) return;

	auto guard = display->lockGuard();

	lv_label_set_text(w.nameLabel, info.connected ? info.hostname : "--");

	// ── 交换模式视觉反馈 ──
	if (m_swapMode)
	{
		if (slot == m_swapSourceSlot)
		{
			// 源槽位：橙色边框
			lv_obj_set_style_border_color(w.card, LV_COLOR_MAKE(0xFF, 0xA0, 0x00), 0);
			lv_obj_set_style_border_width(w.card, 3, 0);
		}
		else if (slot > 0)
		{
			// 其他槽位（含空）：蓝色边框，空槽位可移动
			lv_obj_set_style_border_color(w.card, LV_COLOR_MAKE(0x00, 0x88, 0xFF), 0);
			lv_obj_set_style_border_width(w.card, 3, 0);
		}
	}
	else
	{
		// 清除交换模式残留的边框
		lv_obj_set_style_border_width(w.card, 0, 0);
		// 槽位背景色：连接=亮色，未连接=暗色
		lv_color_t bg = info.connected
			? lv_color_t LV_COLOR_MAKE(0x30, 0x50, 0x70)
			: GUI::Color::CARD;
		lv_obj_set_style_bg_color(w.card, bg, 0);
		lv_obj_set_style_border_width(w.card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(w.card, lv_color_white(), LV_STATE_FOCUSED);
	}

	// 掩码按钮状态恢复
	for (int p = 0; p < 4; p++)
		updateMaskBtnStyle(slot, p);

	// 操作按钮（交换模式隐藏，Slot 0 没有）
	if (slot > 0)
	{
		bool hasClient = info.connected && !m_swapMode;
		if (w.swapBtn)
			lv_obj_set_style_bg_opa(w.swapBtn, hasClient ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		if (w.disconnectBtn)
			lv_obj_set_style_bg_opa(w.disconnectBtn, hasClient ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
	}

	// 如果是从机视图被连接，刷新显示
	if (slot == HostManager::instance().getMySlot() && HostManager::instance().isClient())
	{
		auto guard = display->lockGuard();
		char buf[96];
		snprintf(buf, sizeof(buf), "已连接到 %s (Slot %d)",
			HostManager::instance().getRemoteHostname(),
			HostManager::instance().getMySlot());
		lv_label_set_text(m_connectedInfoLabel, buf);
	}
}

void ConsoleManagerApp::updateMaskBtnStyle(int hostId, int player)
{
	auto& w = m_slotsUI[hostId];
	if (!w.maskBtns[player]) return;

	if (m_maskState[hostId][player])
		lv_obj_set_style_bg_color(w.maskBtns[player], lv_color_hex(0x0088FF), 0);
	else
		lv_obj_set_style_bg_color(w.maskBtns[player], lv_color_hex(0x404050), 0);
}

void ConsoleManagerApp::applyMaskToVirtualGamepad()
{
	uint8_t hostMask[4] = { 0, 0, 0, 0 };
	for (int h = 0; h < 4; h++)
		for (int p = 0; p < 4; p++)
			if (m_maskState[h][p])
				hostMask[h] |= (1 << p);
	VirtualGamepad::instance().setAllMasks(hostMask);
}

// ════════════════════════════════════════════════════════════════
// 槽位操作
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onSlotClick(int slot)
{
	// 暂未使用（槽位点击暂不处理）
}

void ConsoleManagerApp::cancelSwapMode()
{
	if (!m_swapMode) return;
	m_swapMode = false;
	m_swapSourceSlot = -1;
	updateSlotStatus();
	applyFocus();
	ESP_LOGI(TAG, "交换已取消");
}

void ConsoleManagerApp::onSwapSlot(int slotA, int slotB)
{
	if (slotA == slotB || slotA < 1 || slotB < 1 || slotA > 3 || slotB > 3)
	{
		cancelSwapMode();
		return;
	}

	ESP_LOGI(TAG, "交换槽位: %d ↔ %d", slotA, slotB);
	HostManager::instance().swapSlots(slotA, slotB);
	m_swapMode = false;
	m_swapSourceSlot = -1;
	updateSlotStatus();
	applyFocus();
}

void ConsoleManagerApp::onDisconnectSlot(int slot)
{
	if (slot == HostManager::instance().getMySlot() && HostManager::instance().isClient())
	{
		ESP_LOGI(TAG, "从机主动断开连接");
		HostManager::instance().disconnectFromHost();
		applyViewMode();
	}
	else
	{
		// 主机断开某个从机（暂未实现）
		ESP_LOGI(TAG, "主机断开 Slot %d（待实现）", slot);
	}
}

// ════════════════════════════════════════════════════════════════
// 焦点导航
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::applyFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	auto clear = [](lv_obj_t* o) { if (o) lv_obj_clear_state(o, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* o) { if (o) lv_obj_add_state(o, LV_STATE_FOCUSED); };

	clear(m_backBtn);
	clear(m_scanBtn);
	for (auto* r : m_scanRows) clear(r);
	for (int s = 0; s < 4; s++) {
		clear(m_slotsUI[s].card);
		for (int p = 0; p < 4; p++) clear(m_slotsUI[s].maskBtns[p]);
		clear(m_slotsUI[s].swapBtn);
		clear(m_slotsUI[s].disconnectBtn);
	}

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		lv_obj_t* targets[] = { m_backBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 2)
			focus(targets[m_focusTitleIdx]);
		break;
	}
	case FOCUS_LIST:
		if (m_focusListIdx >= 0 && (size_t)m_focusListIdx < m_scanRows.size())
		{
			focus(m_scanRows[m_focusListIdx]);
			lv_obj_scroll_to_view(m_scanRows[m_focusListIdx], LV_ANIM_ON);
		}
		break;
	case FOCUS_SLOTS:
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < 4)
		{
			auto& w = m_slotsUI[m_focusSlotIdx];
			switch (m_slotNavLevel)
			{
			case SLOT_CARD:
				focus(w.card);
				break;
			case SLOT_MASK:
				if (m_focusMaskCol >= 0 && m_focusMaskCol < 4)
					focus(w.maskBtns[m_focusMaskCol]);
				break;
			case SLOT_ACTIONS:
				if (w.swapBtn)
				{
					lv_obj_t* btns[] = { w.swapBtn, w.disconnectBtn };
					if (m_focusActionIdx >= 0 && m_focusActionIdx < 2)
						focus(btns[m_focusActionIdx]);
				}
				break;
			}
		}
		break;
	}
}

void ConsoleManagerApp::activateFocus()
{
	auto guard = display->lockGuard();
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		lv_obj_t* targets[] = { m_backBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 2)
			lv_obj_send_event(targets[m_focusTitleIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_LIST:
	{
		size_t idx = (size_t)m_focusListIdx;
		if (idx < m_connectBtns.size() && m_connectBtns[idx])
			lv_obj_send_event(m_connectBtns[idx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_SLOTS:
	{
		auto& w = m_slotsUI[m_focusSlotIdx];
		switch (m_slotNavLevel)
		{
		case SLOT_CARD:
			if (m_swapMode)
			{
				// 交换模式中 A → 执行交换
				if (m_focusSlotIdx != m_swapSourceSlot)
					onSwapSlot(m_swapSourceSlot, m_focusSlotIdx);
				else
					cancelSwapMode();
			}
			else
			{
				// 进入 SLOT_MASK
				m_slotNavLevel = SLOT_MASK;
				m_focusMaskCol = 0;
				applyFocus();
			}
			break;
		case SLOT_MASK:
			if (m_focusMaskCol >= 0 && m_focusMaskCol < 4)
				lv_obj_send_event(w.maskBtns[m_focusMaskCol], LV_EVENT_CLICKED, nullptr);
			break;
		case SLOT_ACTIONS:
			if (w.swapBtn)
			{
				lv_obj_t* btns[] = { w.swapBtn, w.disconnectBtn };
				if (m_focusActionIdx >= 0 && m_focusActionIdx < 2)
					lv_obj_send_event(btns[m_focusActionIdx], LV_EVENT_CLICKED, nullptr);
			}
			break;
		}
		break;
	}
	}
}

void ConsoleManagerApp::navUp()
{
	switch (m_focusGroup)
	{
	case FOCUS_LIST:
		if (m_focusListIdx > 0) { m_focusListIdx--; applyFocus(); }
		else { m_focusGroup = FOCUS_TITLE; applyFocus(); }
		break;
	case FOCUS_SLOTS:
		if (m_swapMode)
		{
			// 交换模式中 ↑ → 取消交换
			cancelSwapMode();
		}
		else if (m_slotNavLevel == SLOT_CARD)
		{
			m_focusGroup = FOCUS_LIST;
			applyFocus();
		}
		else if (m_slotNavLevel == SLOT_ACTIONS)
		{
			m_slotNavLevel = SLOT_MASK;
			applyFocus();
		}
		else
		{
			m_slotNavLevel = SLOT_CARD;
			applyFocus();
		}
		break;
	case FOCUS_TITLE: break;
	}
}

void ConsoleManagerApp::navDown()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		if (!m_scanRows.empty() && !m_consoles.empty())
		{
			m_focusGroup = FOCUS_LIST;
			m_focusListIdx = 0;
			applyFocus();
		}
		else
		{
			m_focusGroup = FOCUS_SLOTS;
			m_focusSlotIdx = 0;
			m_slotNavLevel = SLOT_CARD;
			applyFocus();
		}
		break;
	case FOCUS_LIST:
		if (m_focusListIdx < (int8_t)m_scanRows.size() - 1) { m_focusListIdx++; applyFocus(); }
		else { m_focusGroup = FOCUS_SLOTS; m_focusSlotIdx = 0; m_slotNavLevel = SLOT_CARD; applyFocus(); }
		break;
	case FOCUS_SLOTS:
		if (m_swapMode)
		{
			// 交换模式：只允许 ←→ 切换槽位，↓ 不生效
		}
		else if (m_slotNavLevel == SLOT_CARD)
		{
			m_slotNavLevel = SLOT_MASK;
			m_focusMaskCol = 0;
			applyFocus();
		}
		else if (m_slotNavLevel == SLOT_MASK)
		{
			if (m_focusSlotIdx > 0 && m_slotsUI[m_focusSlotIdx].swapBtn)
			{
				m_slotNavLevel = SLOT_ACTIONS;
				m_focusActionIdx = 0;
				applyFocus();
			}
			else
			{
				m_slotNavLevel = SLOT_CARD;
				applyFocus();
			}
		}
		else
		{
			m_slotNavLevel = SLOT_CARD;
			applyFocus();
		}
		break;
	}
}

void ConsoleManagerApp::navLeft()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		if (m_focusTitleIdx > 0) { m_focusTitleIdx--; applyFocus(); }
		break;
	case FOCUS_SLOTS:
		switch (m_slotNavLevel)
		{
		case SLOT_CARD:
			if (m_focusSlotIdx > 0) { m_focusSlotIdx--; applyFocus(); }
			break;
		case SLOT_MASK:
			if (m_focusMaskCol > 0) { m_focusMaskCol--; applyFocus(); }
			else if (m_focusSlotIdx > 0) { m_focusSlotIdx--; m_focusMaskCol = 3; applyFocus(); }
			break;
		case SLOT_ACTIONS:
			if (m_focusActionIdx > 0) { m_focusActionIdx--; applyFocus(); }
			break;
		}
		break;
	case FOCUS_LIST:
		break;
	}
}

void ConsoleManagerApp::navRight()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		if (m_focusTitleIdx < 1) { m_focusTitleIdx++; applyFocus(); }
		break;
	case FOCUS_SLOTS:
		switch (m_slotNavLevel)
		{
		case SLOT_CARD:
			if (m_focusSlotIdx < 3) { m_focusSlotIdx++; applyFocus(); }
			break;
		case SLOT_MASK:
			if (m_focusMaskCol < 3) { m_focusMaskCol++; applyFocus(); }
			else if (m_focusSlotIdx < 3) { m_focusSlotIdx++; m_focusMaskCol = 0; applyFocus(); }
			break;
		case SLOT_ACTIONS:
			if (m_focusActionIdx < 1) { m_focusActionIdx++; applyFocus(); }
			break;
		}
		break;
	case FOCUS_LIST:
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── B：返回或取消交换 ──
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (m_swapMode)
		{
			cancelSwapMode();
		}
		else if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + 500;
			Task::addTask([](void* param) -> TickType_t {
				static_cast<ConsoleManagerApp*>(param)->popApp();
				return Task::infinityTime;
				}, "consoleBack", this, 0, Task::Affinity::None);
		}
	}

	// ── A / L3：激活焦点 ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			activateFocus();
			m_nextActionTime = xTaskGetTickCount() + 500;
		}
	}

	// ── 摇杆归位 ──
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[hostId][playerId] = 0;
		return;
	}
	if (m_nextMoveTime[hostId][playerId] >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[hostId][playerId] = xTaskGetTickCount() + delay;

	if (lxLeft)  navLeft();
	if (lxRight) navRight();
	if (lyUp)    navUp();
	if (lyDown)  navDown();
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onScanBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	if (xTaskGetTickCount() < self->m_nextActionTime) return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 1;

	Task::addTask([](void* param) -> TickType_t {
		static_cast<ConsoleManagerApp*>(param)->doScan();
		return Task::infinityTime;
		}, "consoleScan", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onConnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	if (xTaskGetTickCount() < self->m_nextActionTime) return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
	size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(btn);
	self->m_focusGroup = FOCUS_LIST;
	self->m_focusListIdx = (int8_t)idx;

	Task::addTask([](void* param) -> TickType_t {
		auto* p = static_cast<std::pair<ConsoleManagerApp*, size_t>*>(param);
		p->first->doConnect(p->second);
		delete p;
		return Task::infinityTime;
		}, "consoleConnect", new std::pair(self, idx), 0, Task::Affinity::None);
}

void ConsoleManagerApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	if (xTaskGetTickCount() < self->m_nextActionTime) return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	Task::addTask([](void* param) -> TickType_t {
		static_cast<ConsoleManagerApp*>(param)->popApp();
		return Task::infinityTime;
		}, "consoleBack", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onMaskBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	if (xTaskGetTickCount() < self->m_nextActionTime) return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;
	lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
	uint32_t idx = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
	int hostId = (int)(idx / 4);
	int player = (int)(idx % 4);

	self->m_maskState[hostId][player] ^= true;
	self->updateMaskBtnStyle(hostId, player);
	self->applyMaskToVirtualGamepad();

	ESP_LOGI(TAG, "掩码: hostId=%d P%d → %s", hostId, player,
		self->m_maskState[hostId][player] ? "ON" : "OFF");
}

void ConsoleManagerApp::onSwapBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	if (xTaskGetTickCount() < self->m_nextActionTime) return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	// 进入交换模式
	self->m_swapMode = true;
	lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
	self->m_swapSourceSlot = (int8_t)(uintptr_t)lv_obj_get_user_data(btn);

	// 切换到 SLOT_CARD 层级，方便摇杆选目标
	self->m_slotNavLevel = SLOT_CARD;
	self->updateSlotStatus();
	self->applyFocus();
	ESP_LOGI(TAG, "交换模式: 源 Slot %d，请选择目标槽位", self->m_swapSourceSlot);
}

void ConsoleManagerApp::onDisconnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	if (xTaskGetTickCount() < self->m_nextActionTime) return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;
	int slot;

	if (lv_event_get_target(e) == self->m_disconnectBtn)
	{
		// 从机视图的断开按钮
		self->onDisconnectSlot(HostManager::instance().getMySlot());
		return;
	}

	lv_obj_t* btn2 = (lv_obj_t*)lv_event_get_target(e);
	slot = (int)(uintptr_t)lv_obj_get_user_data(btn2);
	self->onDisconnectSlot(slot);
}

// ════════════════════════════════════════════════════════════════
// AI 能力
// ════════════════════════════════════════════════════════════════

std::string ConsoleManagerApp::getAiContext()
{
	std::string ctx = "当前在多主机管理。\n"
		"可用焦点组（select 的 text 参数）：\n"
		"- title(0-1)\t返回/扫描\n"
		"- list(0-" + std::to_string(m_consoles.empty() ? 0 : (int)m_consoles.size() - 1) + ")\t主机列表\n"
		"- slots(0-3)\t槽位区域\n"
		"- mask(row,col)\t掩码矩阵(0-3,0-3)\n\n";

	ctx += "发现 " + std::to_string(m_consoles.size()) + " 台主机\n";
	ctx += std::string("角色: ") + (HostManager::instance().isHost() ? "主机" : "从机") + "\n";

	const char* groupName = "";
	switch (m_focusGroup) {
	case FOCUS_TITLE: groupName = "title"; break;
	case FOCUS_LIST:  groupName = "list"; break;
	case FOCUS_SLOTS: groupName = "slots"; break;
	}
	ctx += std::string("焦点@") + groupName;
	ctx += "\n";
	return ctx;
}

void ConsoleManagerApp::onAiSelect(const AiSelectTarget& target)
{
	auto guard = display->lockGuard();
	if (!guard) return;

	if (target.group == "title" && target.index >= 0 && target.index <= 1) {
		m_focusGroup = FOCUS_TITLE;
		m_focusTitleIdx = (int8_t)target.index;
		applyFocus();
	}
	else if (target.group == "list" && target.index >= 0 && (size_t)target.index < m_scanRows.size()) {
		m_focusGroup = FOCUS_LIST;
		m_focusListIdx = (int8_t)target.index;
		applyFocus();
	}
	else if (target.group == "slots" && target.index >= 0 && target.index < 4) {
		m_focusGroup = FOCUS_SLOTS;
		m_focusSlotIdx = (int8_t)target.index;
		applyFocus();
	}
	else if (target.group == "mask") {
		m_focusGroup = FOCUS_SLOTS;
		m_slotNavLevel = SLOT_MASK;
		m_focusSlotIdx = target.index;
		// col from target or default 0
		applyFocus();
	}
}

void ConsoleManagerApp::onAiActivate()
{
	activateFocus();
}
