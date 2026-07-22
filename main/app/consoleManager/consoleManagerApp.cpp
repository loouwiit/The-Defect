#include "consoleManagerApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "discovery/discovery.hpp"
#include "host/hostManager.hpp"
#include "host/virtualGamepad.hpp"
#include "wifi/mdns.hpp"  // mdnsGetConfiguredHostname
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

	buildUi();

	m_focusGroup = FOCUS_TITLE;
	m_focusTitleIdx = 0;
	m_focusListIdx = 0;
	applyFocus();

	// 注册槽位更新回调 — 始终生效（主机/从机均需要）
	HostManager::instance().setOnSlotUpdate([this](int) {
		updateSlots();
	});

	// 初始更新槽位显示
	updateSlots();

	// 如果已经是连接状态（从其他 App 返回），切换到从机视图
	if (HostManager::instance().isClient())
	{
		switchToConnectedView();
	}

	// 定时器
	m_refreshTimer = lv_timer_create(timerCb, RefreshInterval, this);

	ESP_LOGI(TAG, "多主机管理 App 初始化完成");
}

void ConsoleManagerApp::deinit()
{
	// 停止定时器
	if (m_refreshTimer)
	{
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}

	running = false;
	deletable = !m_scanActive;  // 扫描中则延迟析构，doScan 收尾时会置 true

	if (m_scanActive)
		ESP_LOGI(TAG, "扫描进行中，延迟销毁");
	else
		ESP_LOGI(TAG, "多主机管理 App 已释放");
}

void ConsoleManagerApp::onForeground()
{
	auto guard = display->lockGuard();
	if (m_refreshTimer)
		lv_timer_resume(m_refreshTimer);
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + 500;

	// 恢复正确的视图
	if (HostManager::instance().isClient())
	{
		if (!m_connectedView) switchToConnectedView();
	}
	else
	{
		if (m_connectedView) switchToScanView();
	}

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
// 定时器
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::timerCb(lv_timer_t* t)
{
	static_cast<ConsoleManagerApp*>(lv_timer_get_user_data(t))->refreshUi();
}

void ConsoleManagerApp::refreshUi()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	updateStatus();
	if (m_connectedView || HostManager::instance().isClient())
		updateSlots();

	// 扫描状态检查
	if (m_scanActive)
	{
		// 重建列表（结果已由 doScan 更新）
		updateScanList();

		// 检查是否超时（doScan 同步完成，m_scanActive 已置 false）
		if (!m_scanActive)
		{
			lv_label_set_text(m_scanBtnLabel, "扫描");
		}
	}
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::buildUi()
{
	auto guard = display->lockGuard();

	lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 顶部栏 ──
	auto top_bar = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
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

	// ── 列表区域（flex_grow 填充） ──
	auto list_section = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(list_section, 1);
	lv_obj_set_style_pad_all(list_section, 8, 0);
	lv_obj_set_style_pad_left(list_section, 16, 0);
	lv_obj_set_style_pad_right(list_section, 16, 0);
	lv_obj_set_style_border_width(list_section, 0, 0);
	lv_obj_set_style_bg_opa(list_section, LV_OPA_TRANSP, 0);

	auto list_title = GUI::createSubtitle(list_section, "发现的主机");
	lv_obj_set_style_text_font(list_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	m_scanListContainer = lv_obj_create(list_section);
	lv_obj_set_width(m_scanListContainer, lv_pct(100));
	lv_obj_set_flex_grow(m_scanListContainer, 1);
	lv_obj_set_style_border_width(m_scanListContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_scanListContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_scanListContainer, 0, 0);
	lv_obj_set_layout(m_scanListContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_scanListContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollbar_mode(m_scanListContainer, LV_SCROLLBAR_MODE_AUTO);

	// 初始空提示
	auto empty_hint = GUI::createLabel(m_scanListContainer, "点击「扫描」发现局域网内的其他主机");
	lv_obj_set_style_text_color(empty_hint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(empty_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(empty_hint, 20, 0);
	lv_obj_set_width(empty_hint, lv_pct(100));
	lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
	m_scanRows.push_back(empty_hint);

	// ── 槽位区域（始终显示）──
	buildSlots();

	// ── 底部状态 ──
	m_statusLabel = GUI::createLabel(screen, "就绪");
	lv_obj_set_width(m_statusLabel, lv_pct(100));
	lv_obj_set_style_text_align(m_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_font(m_statusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_pad_all(m_statusLabel, 8, 0);
	lv_obj_set_style_pad_bottom(m_statusLabel, 12, 0);

	// ── 连接状态容器（初始隐藏，连接到主机后显示）──
	m_connectedContainer = lv_obj_create(screen);
	lv_obj_set_size(m_connectedContainer, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(m_connectedContainer, 1);
	lv_obj_set_style_border_width(m_connectedContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_connectedContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(m_connectedContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_connectedContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(m_connectedContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(m_connectedContainer, 40, 0);
	lv_obj_add_flag(m_connectedContainer, LV_OBJ_FLAG_HIDDEN);

	m_connectedLabel = GUI::createTitle(m_connectedContainer, "已连接到主机");
	lv_obj_set_style_text_font(m_connectedLabel, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_set_style_pad_bottom(m_connectedLabel, 24, 0);

	m_disconnectBtn = GUI::createButton(m_connectedContainer, "断开连接", 160, 50);
	lv_obj_set_style_radius(m_disconnectBtn, 12, 0);
	lv_obj_set_style_bg_color(m_disconnectBtn, LV_COLOR_MAKE(0xAA, 0x33, 0x33), 0);
	lv_obj_set_style_border_width(m_disconnectBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_disconnectBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_disconnectBtn, onDisconnectBtnCb, LV_EVENT_CLICKED, this);
}

// ════════════════════════════════════════════════════════════════
// 扫描
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::doScan()
{
	if (!running)
	{
		// App 已停止，不需要再扫描
		// 如果 deletable 仍为 false，说明 deinit 在等我们收尾
		if (!deletable)
		{
			deletable = true;
			ESP_LOGI(TAG, "延迟销毁完成（扫描未实际执行）");
		}
		return;
	}

	if (m_scanActive)
	{
		ESP_LOGW(TAG, "扫描中，跳过");
		return;
	}

	m_scanActive = true;

	{
		auto guard = display->lockGuard();
		lv_label_set_text(m_scanBtnLabel, "搜索中...");
		lv_label_set_text(m_statusLabel, "正在扫描...");
	}

	ESP_LOGI(TAG, "开始 mDNS 扫描...");

	DiscoveredConsole buffer[MaxConsoles];
	int count = discoverConsoles(buffer, MaxConsoles, ScanTimeoutMs);

	// 更新结果（数据操作不依赖 LVGL）
	m_consoles.clear();
	for (int i = 0; i < count; i++)
		m_consoles.push_back(buffer[i]);

	m_scanActive = false;

	// ── 如果扫描期间 deinit 被调用了 ──
	if (!running)
	{
		deletable = true;
		ESP_LOGI(TAG, "延迟销毁完成，扫描结果已丢弃");
		return;
	}

	// 更新 UI
	{
		auto guard = display->lockGuard();
		lv_label_set_text(m_scanBtnLabel, "扫描");
		updateScanList();
		updateStatus();
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

	// ── 通过 HostManager 连接远程主机（将放弃本机主机资格）──
	if (!HostManager::instance().connectToHost(c.ip, 8080))
	{
		ESP_LOGE(TAG, "连接失败");
		auto guard = display->lockGuard();
		lv_label_set_text(m_statusLabel, "连接失败");
		return;
	}

	ESP_LOGI(TAG, "已连接到 %s，成为从机", c.hostname);

	// ── 切换到从机视图 ──
	switchToConnectedView();
}

void ConsoleManagerApp::disconnectFromHost()
{
	ESP_LOGI(TAG, "断开与主机的连接");

	// 断开 HostManager 连接（恢复主机身份）
	HostManager::instance().disconnectFromHost();

	// 重新注册回调（断开时会被清空，重新注册保持主机侧响应）
	HostManager::instance().setOnSlotUpdate([this](int) {
		updateSlots();
	});

	// 更新槽位显示
	updateSlots();

	// 切回扫描视图
	switchToScanView();
}

void ConsoleManagerApp::switchToConnectedView()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	m_connectedView = true;

	// 隐藏扫描相关控件
	lv_obj_add_flag(m_scanBtn, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_scanListContainer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_statusLabel, LV_OBJ_FLAG_HIDDEN);

	// 显示连接状态区域
	lv_obj_clear_flag(m_connectedContainer, LV_OBJ_FLAG_HIDDEN);
	updateSlots();

	const char* hostname = HostManager::instance().getRemoteHostname();
	if (hostname && hostname[0] != '\0')
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "已连接到 %s", hostname);
		lv_label_set_text(m_connectedLabel, buf);
	}
	else
	{
		lv_label_set_text(m_connectedLabel, "已连接到主机");
	}

	ESP_LOGI(TAG, "切换到从机视图");
}

void ConsoleManagerApp::switchToScanView()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	m_connectedView = false;

	// 隐藏连接状态区域（槽位始终显示）
	lv_obj_add_flag(m_connectedContainer, LV_OBJ_FLAG_HIDDEN);

	// 恢复扫描相关控件
	lv_obj_clear_flag(m_scanBtn, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_scanListContainer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_statusLabel, LV_OBJ_FLAG_HIDDEN);

	lv_label_set_text(m_statusLabel, "就绪");

	ESP_LOGI(TAG, "切回扫描视图");
}

// ════════════════════════════════════════════════════════════════
// 槽位 UI
// ════════════════════════════════════════════════════════════════

static constexpr lv_coord_t SLOT_W = 145;
static constexpr lv_coord_t SLOT_H = 110;
static constexpr lv_coord_t MASK_BTN_SIZE = 28;

void ConsoleManagerApp::buildSlots()
{
	auto guard = display->lockGuard();

	m_slotsContainer = lv_obj_create(screen);
	lv_obj_set_width(m_slotsContainer, lv_pct(100));
	lv_obj_set_height(m_slotsContainer, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(m_slotsContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_slotsContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(m_slotsContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_slotsContainer, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(m_slotsContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(m_slotsContainer, 4, 0);
	lv_obj_set_style_pad_column(m_slotsContainer, 6, 0);
	lv_obj_set_style_pad_bottom(m_slotsContainer, 8, 0);

	for (int slot = 0; slot < 4; slot++)
	{
		// ── 卡片 ──
		auto card = lv_obj_create(m_slotsContainer);
		lv_obj_set_size(card, SLOT_W, SLOT_H);
		lv_obj_set_style_radius(card, 8, 0);
		lv_obj_set_style_bg_color(card, GUI::Color::CARD, 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(card, 2, 0);
		lv_obj_set_style_border_color(card, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(card, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_layout(card, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(card, 4, 0);
		lv_obj_set_user_data(card, (void*)(uintptr_t)slot);
		lv_obj_add_event_cb(card, onSlotClickCb, LV_EVENT_CLICKED, this);

		// ── 第一行：Slot 编号 + 主机名 ──
		char label[32];
		snprintf(label, sizeof(label), "Slot %d:", slot);
		auto nameLabel = GUI::createLabel(card, label);
		lv_obj_set_style_text_font(nameLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_align(nameLabel, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_width(nameLabel, lv_pct(100));
		lv_obj_set_flex_grow(nameLabel, 1);
		lv_label_set_long_mode(nameLabel, LV_LABEL_LONG_DOT);
		m_slotNameLabels[slot] = nameLabel;

		// ── 第二行：4 个掩码按钮 ──
		auto maskRow = lv_obj_create(card);
		lv_obj_set_width(maskRow, lv_pct(100));
		lv_obj_set_height(maskRow, LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(maskRow, 0, 0);
		lv_obj_set_style_bg_opa(maskRow, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(maskRow, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(maskRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(maskRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_column(maskRow, 4, 0);
		lv_obj_set_style_pad_all(maskRow, 0, 0);
		lv_obj_remove_flag(maskRow, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_clear_flag(maskRow, LV_OBJ_FLAG_CLICKABLE);

		for (int p = 0; p < 4; p++)
		{
			auto btn = lv_obj_create(maskRow);
			lv_obj_set_size(btn, MASK_BTN_SIZE, MASK_BTN_SIZE);
			lv_obj_set_style_radius(btn, 4, 0);
			lv_obj_set_style_border_width(btn, 1, 0);
			lv_obj_set_style_border_color(btn, GUI::Color::SUBTLE, 0);
			lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUSED);
			lv_obj_set_style_border_color(btn, lv_color_white(), LV_STATE_FOCUSED);
			lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
			lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_set_user_data(btn, (void*)(uintptr_t)(slot * 4 + p));
			lv_obj_add_event_cb(btn, onMaskBtnCb, LV_EVENT_CLICKED, this);
			m_maskBtns[slot][p] = btn;
		}
	}

	// 初始更新
	updateSlots();
}

void ConsoleManagerApp::updateSlots()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	auto& hm = HostManager::instance();

	if (hm.isHost())
	{
		// ── 主机视图：Slot 0 = 本机，Slot 1-3 = 已连接的从机 ──
		const auto* slots = hm.getSlots();
		for (int slot = 0; slot < 4; slot++)
		{
			const auto& s = slots[slot];
			char label[80];
			if (s.connected && s.hostname[0] != '\0')
				snprintf(label, sizeof(label), "S%d:%s", slot, s.hostname);
			else
				snprintf(label, sizeof(label), "S%d:--", slot);
			lv_label_set_text(m_slotNameLabels[slot], label);
		}
	}
	else
	{
		// ── 从机视图：Slot 0 = 远程主机，mySlot = 本机，其余由广播更新 ──
		int8_t mySlot = hm.myAssignedSlot();
		const char* remoteHost = hm.getRemoteHostname();

		for (int slot = 0; slot < 4; slot++)
		{
			char label[80];
			if (slot == 0)
			{
				// Slot 0 始终显示远程主机
				if (remoteHost && remoteHost[0] != '\0')
					snprintf(label, sizeof(label), "S0:%s", remoteHost);
				else
					snprintf(label, sizeof(label), "S0:主机");
			}
			else if (slot == mySlot)
			{
				// 本设备的槽位
				snprintf(label, sizeof(label), "S%d:%s(我)", slot,
					mdnsGetConfiguredHostname());
			}
			else
			{
				// 其他从机（通过 client_joined 广播获取）
				const auto* slots = hm.getSlots();
				if (slot < 4 && slots[slot].connected && slots[slot].hostname[0] != '\0')
					snprintf(label, sizeof(label), "S%d:%s", slot, slots[slot].hostname);
				else
					snprintf(label, sizeof(label), "S%d:--", slot);
			}
			lv_label_set_text(m_slotNameLabels[slot], label);
		}
	}

	// 更新掩码按钮
	for (int slot = 0; slot < 4; slot++)
		updateMaskBtnVisual(slot);
}

void ConsoleManagerApp::updateMaskBtnVisual(int slot)
{
	const uint16_t* mask = VirtualGamepad::instance().getInputMask();
	uint16_t slotMask = mask[slot];

	for (int p = 0; p < 4; p++)
	{
		bool enabled = (slotMask & (1u << p)) != 0;
		lv_obj_t* btn = m_maskBtns[slot][p];
		if (enabled)
		{
			lv_obj_set_style_bg_color(btn, GUI::Color::PRIMARY, 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(btn, GUI::Color::PRIMARY, 0);
		}
		else
		{
			lv_obj_set_style_bg_color(btn, GUI::Color::CARD, 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
			lv_obj_set_style_border_color(btn, GUI::Color::SUBTLE, 0);
		}
	}
}

void ConsoleManagerApp::doSwapSlots(int slotA, int slotB)
{
	if (slotA == slotB) return;
	if (slotA == 0 || slotB == 0)
	{
		ESP_LOGW(TAG, "不能交换 Slot 0（本机）");
		return;
	}

	ESP_LOGI(TAG, "交换 Slot %d <-> %d", slotA, slotB);
	HostManager::instance().swapSlots(slotA, slotB);
	updateSlots();
	m_swapSelectedSlot = -1;
}

// ════════════════════════════════════════════════════════════════
// 列表更新
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::updateScanList()
{
	auto guard = display->lockGuard();
	// 删除旧行
	for (auto* row : m_scanRows)
	{
		if (row) lv_obj_del(row);
	}
	m_scanRows.clear();
	m_connectBtns.clear();

	if (m_consoles.empty())
	{
		auto hint = GUI::createLabel(m_scanListContainer,
			m_scanActive ? "正在搜索..." : "未发现其他主机");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_pad_top(hint, 20, 0);
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
		lv_obj_set_style_pad_all(row, 8, 0);
		lv_obj_set_style_pad_left(row, 12, 0);
		lv_obj_set_style_pad_right(row, 8, 0);
		lv_obj_set_user_data(row, (void*)(uintptr_t)i);

		// 左侧：主机名 + IP
		auto left_flex = lv_obj_create(row);
		lv_obj_set_height(left_flex, lv_pct(100));
		lv_obj_set_flex_grow(left_flex, 1);
		lv_obj_remove_flag(left_flex, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_remove_flag(left_flex, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_border_width(left_flex, 0, 0);
		lv_obj_set_style_bg_opa(left_flex, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(left_flex, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(left_flex, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(left_flex, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
		lv_obj_set_style_pad_all(left_flex, 0, 0);

		auto name_label = GUI::createLabel(left_flex, c.hostname);
		lv_obj_set_style_text_font(name_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_remove_flag(name_label, LV_OBJ_FLAG_SCROLLABLE);

		char ipStr[32];
		snprintf(ipStr, sizeof(ipStr), "%s:%u", c.ip, c.port);
		auto ip_label = GUI::createLabel(left_flex, ipStr);
		lv_obj_set_style_text_font(ip_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(ip_label, GUI::Color::SUBTLE, 0);
		lv_obj_remove_flag(ip_label, LV_OBJ_FLAG_SCROLLABLE);

		// 右侧：连接按钮
		auto connectBtn = GUI::createButton(row, "连接", 72, 32);
		lv_obj_set_style_radius(connectBtn, 6, 0);
		lv_obj_set_style_bg_color(connectBtn, LV_COLOR_MAKE(0x00, 0x77, 0x00), 0);
		lv_obj_set_user_data(connectBtn, (void*)(uintptr_t)i);
		lv_obj_add_event_cb(connectBtn, onConnectBtnCb, LV_EVENT_CLICKED, this);

		m_scanRows.push_back(row);
		m_connectBtns.push_back(connectBtn);
	}
}

void ConsoleManagerApp::updateStatus()
{
	char buf[64];
	if (m_consoles.empty())
		snprintf(buf, sizeof(buf), "未发现主机");
	else
		snprintf(buf, sizeof(buf), "发现 %zu 台主机", m_consoles.size());

	auto guard = display->lockGuard();
	lv_label_set_text(m_statusLabel, buf);
}

// ════════════════════════════════════════════════════════════════
// 焦点导航
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::applyFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	auto clearFocus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clearFocus(m_backBtn);
	clearFocus(m_scanBtn);
	for (auto* row : m_scanRows) clearFocus(row);
	for (int s = 0; s < 4; s++) {
		// 清除槽位卡片的焦点
		auto child = lv_obj_get_child(m_slotsContainer, s);
		if (child) clearFocus(static_cast<lv_obj_t*>(child));
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
			auto child = lv_obj_get_child(m_slotsContainer, m_focusSlotIdx);
			if (child) focus((lv_obj_t*)child);
		}
		break;

	default:
		break;
	}
}

void ConsoleManagerApp::activateFocus()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		auto guard = display->lockGuard();
		lv_obj_t* targets[] = { m_backBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 2)
			lv_obj_send_event(targets[m_focusTitleIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_LIST:
	{
		auto guard = display->lockGuard();
		size_t idx = (size_t)m_focusListIdx;
		if (idx < m_connectBtns.size() && m_connectBtns[idx])
			lv_obj_send_event(m_connectBtns[idx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_SLOTS:
	{
		auto guard = display->lockGuard();
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < 4)
		{
			auto child = lv_obj_get_child(m_slotsContainer, m_focusSlotIdx);
			if (child) lv_obj_send_event((lv_obj_t*)child, LV_EVENT_CLICKED, nullptr);
		}
		break;
	}

	default:
		break;
	}
}

void ConsoleManagerApp::navTitleLeft()
{
	auto guard = display->lockGuard();
	if (m_focusTitleIdx > 0) m_focusTitleIdx--;
	applyFocus();
}

void ConsoleManagerApp::navTitleRight()
{
	auto guard = display->lockGuard();
	if (m_focusTitleIdx < 1) m_focusTitleIdx++;
	applyFocus();
}

void ConsoleManagerApp::navListUp()
{
	auto guard = display->lockGuard();
	if (m_focusListIdx > 0)
	{
		m_focusListIdx--;
		applyFocus();
	}
	else
	{
		m_focusGroup = FOCUS_TITLE;
		applyFocus();
	}
}

void ConsoleManagerApp::navListDown()
{
	auto guard = display->lockGuard();
	int8_t last = (int8_t)m_scanRows.size() - 1;
	if (m_focusListIdx < last)
	{
		m_focusListIdx++;
		applyFocus();
	}
	// 已在底部，不循环
}

void ConsoleManagerApp::navSlotLeft()
{
	auto guard = display->lockGuard();
	if (m_focusSlotIdx > 0) { m_focusSlotIdx--; applyFocus(); }
}

void ConsoleManagerApp::navSlotRight()
{
	auto guard = display->lockGuard();
	if (m_focusSlotIdx < 3) { m_focusSlotIdx++; applyFocus(); }
}

void ConsoleManagerApp::navSlotUp()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_TITLE;
	applyFocus();
}

void ConsoleManagerApp::navSlotDown()
{
	auto guard = display->lockGuard();
	if (m_connectedView) {
		// 在连接视图中，向下到断开按钮
	}
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onGamepadInput(int8_t hostId, int8_t playerId, const GamepadState& state)
{
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── B：返回 ──
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + 500;
			Task::addTask([](void* param) -> TickType_t
				{
					static_cast<ConsoleManagerApp*>(param)->popApp();
					return Task::infinityTime;
				}, "consoleBack", this, 0, Task::Affinity::None);
		}
		return;
	}

	// ── A / L3：激活焦点 ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		activateFocus();
	}

	// ── 摇杆归位 ──
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[playerId] = 0;
		return;
	}
	if (m_nextMoveTime[playerId] >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		if (lxLeft)  navTitleLeft();
		if (lxRight) navTitleRight();
		if (lyDown)
		{
			if (m_connectedView)
			{
				// 连接视图下 → 切到槽位
				m_focusGroup = FOCUS_SLOTS;
				m_focusSlotIdx = 0;
				applyFocus();
			}
			else if (!m_scanRows.empty())
			{
				m_focusGroup = FOCUS_LIST;
				m_focusListIdx = 0;
				applyFocus();
			}
		}
		break;

	case FOCUS_LIST:
		if (lyUp)   navListUp();
		if (lyDown) navListDown();
		break;

	case FOCUS_SLOTS:
		if (lxLeft)  navSlotLeft();
		if (lxRight) navSlotRight();
		if (lyDown)  navSlotDown();
		if (lyUp)    navSlotUp();
		break;

	case FOCUS_MASK:
		if (lxLeft)  { m_focusMaskSlotIdx = 0; m_focusGroup = FOCUS_SLOTS; applyFocus(); }
		if (lxRight) { m_focusMaskSlotIdx = 3; m_focusGroup = FOCUS_SLOTS; applyFocus(); }
		if (lyUp)    { m_focusGroup = FOCUS_SLOTS; applyFocus(); }
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onScanBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 1;

	Task::addTask([](void* param) -> TickType_t
		{
			static_cast<ConsoleManagerApp*>(param)->doScan();
			return Task::infinityTime;
		}, "consoleScan", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onConnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(btn);
	self->m_focusGroup = FOCUS_LIST;
	self->m_focusListIdx = (int8_t)idx;

	// 连接操作在 Task 中执行
	Task::addTask([](void* param) -> TickType_t
		{
			auto* p = static_cast<std::pair<ConsoleManagerApp*, size_t>*>(param);
			p->first->doConnect(p->second);
			delete p;
			return Task::infinityTime;
		}, "consoleConnect", new std::pair(self, idx), 0, Task::Affinity::None);
}

void ConsoleManagerApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 0;

	if (xTaskGetTickCount() < self->m_nextActionTime)
		return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	Task::addTask([](void* param) -> TickType_t
		{
			static_cast<ConsoleManagerApp*>(param)->popApp();
			return Task::infinityTime;
		}, "consoleBack", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onDisconnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
		return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	// 在 Task 中执行断开逻辑
	Task::addTask([](void* param) -> TickType_t
		{
			static_cast<ConsoleManagerApp*>(param)->disconnectFromHost();
			return Task::infinityTime;
		}, "consoleDisconnect", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onSlotClickCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	auto target = static_cast<lv_obj_t*>(lv_event_get_target(e));
	int slot = (int)(uintptr_t)lv_obj_get_user_data(target);

	const auto* slots = HostManager::instance().getSlots();
	if (!slots[slot].connected)
	{
		ESP_LOGW(TAG, "Slot %d 为空，不能交换", slot);
		return;
	}

	if (self->m_swapSelectedSlot < 0)
	{
		// 第一次选择：标记交换源
		self->m_swapSelectedSlot = slot;
		lv_obj_set_style_border_color(target, LV_COLOR_MAKE(0xFF, 0xCC, 0x00), 0);
		ESP_LOGI(TAG, "选择 Slot %d 作为交换源", slot);
	}
	else if (self->m_swapSelectedSlot == slot)
	{
		// 点同一个：取消选择
		self->m_swapSelectedSlot = -1;
		self->updateSlots();
		ESP_LOGI(TAG, "取消交换");
	}
	else
	{
		// 第二次选择：执行交换
		int src = self->m_swapSelectedSlot;
		self->doSwapSlots(src, slot);
		// 取消高亮（updateSlots 内已刷新）
		self->updateSlots();
	}
}

void ConsoleManagerApp::onMaskBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	auto btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	int idx = (int)(uintptr_t)lv_obj_get_user_data(btn);
	int slot = idx / 4;
	int player = idx % 4;

	// 切换该 slot 的对应 player bit
	uint16_t currentMask[4];
	const uint16_t* mask = VirtualGamepad::instance().getInputMask();
	for (int i = 0; i < 4; i++) currentMask[i] = mask[i];

	currentMask[slot] ^= (1u << player);

	VirtualGamepad::instance().setInputMask(currentMask);
	self->updateMaskBtnVisual(slot);

	ESP_LOGI(TAG, "掩码: Slot%d P%d %s", slot, player,
		(currentMask[slot] & (1u << player)) ? "ON" : "OFF");

	// 广播掩码给所有从机（限速：最快每 200ms 一次）
	static TickType_t s_lastMaskBcast = 0;
	TickType_t now = xTaskGetTickCount();
	if (now - s_lastMaskBcast >= pdMS_TO_TICKS(200)) {
		s_lastMaskBcast = now;
		char maskMsg[128];
		int mn = snprintf(maskMsg, sizeof(maskMsg),
			"{\"type\":\"mask_sync\",\"mask\":[%u,%u,%u,%u]}",
			currentMask[0], currentMask[1], currentMask[2], currentMask[3]);
		if (mn > 0) HostManager::instance().broadcast(maskMsg, mn);
	}
}

// ════════════════════════════════════════════════════════════════
// AI 能力
// ════════════════════════════════════════════════════════════════

std::string ConsoleManagerApp::getAiContext()
{
	std::string ctx = "当前在多主机管理。\n"
		"可用焦点组（select 的 text 参数）：\n"
		"- title(0-1)\t返回/扫描\n"
		"- list(0-" + std::to_string(m_consoles.empty() ? 0 : (int)m_consoles.size() - 1) + ")\t主机列表\n\n";

	ctx += "发现 " + std::to_string(m_consoles.size()) + " 台主机\n";

	const char* groupName = "";
	switch (m_focusGroup) {
	case FOCUS_TITLE: groupName = "title"; break;
	case FOCUS_LIST:  groupName = "list"; break;
	case FOCUS_SLOTS: groupName = "slots"; break;
	case FOCUS_MASK:  groupName = "mask"; break;
	}
	ctx += std::string("焦点@") + groupName;
	if (m_focusGroup == FOCUS_TITLE) ctx += ":" + std::to_string(m_focusTitleIdx);
	else if (m_focusGroup == FOCUS_LIST) ctx += ":" + std::to_string(m_focusListIdx);
	else if (m_focusGroup == FOCUS_SLOTS) ctx += ":" + std::to_string(m_focusSlotIdx);
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
}

void ConsoleManagerApp::onAiActivate()
{
	activateFocus();
}
