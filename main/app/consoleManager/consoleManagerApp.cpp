#include "consoleManagerApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "host/hostManager.hpp"
#include "discovery/discovery.hpp"
#include "gamepad/virtualGamepad.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <utility>

// ════════════════════════════════════════════════════════════════
// 颜色
// ════════════════════════════════════════════════════════════════
static constexpr uint32_t C_SLOT_EMPTY = 0x1E1E2E;
static constexpr uint32_t C_SLOT_OCCUPIED = 0x2D2D3D;
static constexpr uint32_t C_SLOT_SELF = 0x1A3A5E;
static constexpr uint32_t C_MASK_ON = 0x4FC3F7;
static constexpr uint32_t C_MASK_OFF = 0x444455;
static constexpr uint32_t C_SWAP_SEL = 0x4A6CF7;

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
	if (!guard) { ESP_LOGE(TAG, "无法获取 LVGL 锁"); return; }

	lv_obj_set_style_bg_color(screen, GUI::Color::BG, 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	buildUi();

	// 监听 HostManager 槽位变化
	HostManager::instance().setOnSlotUpdate([this](const auto* slots) {
		// 在 LVGL 安全上下文中刷新
		if (auto g = display->lockGuard()) {
			updateSlots();
			updateStatus();
		}
	});

	m_focusGroup = FOCUS_TITLE;
	m_focusTitleIdx = 0;
	m_focusListIdx = 0;
	applyFocus();

	m_refreshTimer = lv_timer_create(timerCb, RefreshInterval, this);
	ESP_LOGI(TAG, "多主机管理 App 初始化完成");
}

void ConsoleManagerApp::deinit()
{
	if (m_refreshTimer) { lv_timer_del(m_refreshTimer); m_refreshTimer = nullptr; }
	running = false;
	deletable = !m_scanActive;
	if (m_scanActive) ESP_LOGI(TAG, "扫描进行中，延迟销毁");
	else ESP_LOGI(TAG, "已释放");
}

void ConsoleManagerApp::onForeground()
{
	auto guard = display->lockGuard();
	if (m_refreshTimer) lv_timer_resume(m_refreshTimer);
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + 500;
	updateSlots();
	updateStatus();
	ESP_LOGI(TAG, "前台");
}

void ConsoleManagerApp::onBackground()
{
	auto guard = display->lockGuard();
	if (m_refreshTimer) lv_timer_pause(m_refreshTimer);
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
	if (m_scanActive) {
		updateScanList();
		if (!m_scanActive) lv_label_set_text(m_scanBtnLabel, "扫描");
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
	auto topBar = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(topBar, 8, 0);
	lv_obj_set_style_pad_left(topBar, 16, 0);
	lv_obj_set_style_pad_right(topBar, 16, 0);
	lv_obj_set_style_border_width(topBar, 0, 0);
	lv_obj_set_style_bg_opa(topBar, LV_OPA_TRANSP, 0);

	m_backBtn = GUI::createButton(topBar, "返回", 100, 44);
	lv_obj_set_style_bg_color(m_backBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_backBtn, onBackBtnCb, LV_EVENT_CLICKED, this);

	auto title = GUI::createTitle(topBar, "多主机管理");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	m_scanBtn = GUI::createButton(topBar, "扫描", 100, 40);
	m_scanBtnLabel = lv_obj_get_child(m_scanBtn, 0);
	lv_obj_set_style_border_width(m_scanBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_scanBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_scanBtn, onScanBtnCb, LV_EVENT_CLICKED, this);

	// ── 发现的主机列表 ──
	auto listSection = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	listSection = listSection;  // suppress unused
	lv_obj_set_flex_grow(listSection, 1);
	lv_obj_set_style_pad_left(listSection, 16, 0);
	lv_obj_set_style_pad_right(listSection, 16, 0);
	lv_obj_set_style_pad_top(listSection, 8, 0);
	lv_obj_set_style_border_width(listSection, 0, 0);
	lv_obj_set_style_bg_opa(listSection, LV_OPA_TRANSP, 0);

	auto listTitle = GUI::createSubtitle(listSection, "发现的主机");
	listTitle = listTitle;

	m_scanListContainer = lv_obj_create(listSection);
	lv_obj_set_width(m_scanListContainer, lv_pct(100));
	lv_obj_set_flex_grow(m_scanListContainer, 1);
	lv_obj_set_style_border_width(m_scanListContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_scanListContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_scanListContainer, 0, 0);
	lv_obj_set_layout(m_scanListContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_scanListContainer, LV_FLEX_FLOW_COLUMN);

	auto hint = GUI::createLabel(m_scanListContainer, "点击「扫描」发现局域网内的其他主机");
	lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(hint, 20, 0);
	lv_obj_set_width(hint, lv_pct(100));
	lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
	m_scanRows.push_back(hint);

	// ── 4 槽位 ──
	buildSlots();

	// ── 底部状态 + 断开 ──
	auto bottomBar = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(bottomBar, 8, 0);
	lv_obj_set_style_pad_left(bottomBar, 16, 0);
	lv_obj_set_style_pad_right(bottomBar, 16, 0);
	lv_obj_set_style_pad_bottom(bottomBar, 12, 0);
	lv_obj_set_style_border_width(bottomBar, 0, 0);
	lv_obj_set_style_bg_opa(bottomBar, LV_OPA_TRANSP, 0);

	m_connStatusLabel = GUI::createLabel(bottomBar, "就绪");
	lv_obj_set_flex_grow(m_connStatusLabel, 1);
	lv_obj_set_style_text_font(m_connStatusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_connStatusLabel, GUI::Color::SUBTLE, 0);

	updateStatus();
}

void ConsoleManagerApp::buildSlots()
{
	auto guard = display->lockGuard();

	auto section = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	m_slotContainer = section;
	lv_obj_set_style_pad_all(section, 4, 0);
	lv_obj_set_style_pad_left(section, 12, 0);
	lv_obj_set_style_pad_right(section, 12, 0);
	lv_obj_set_style_border_width(section, 0, 0);
	lv_obj_set_style_bg_opa(section, LV_OPA_TRANSP, 0);

	auto slotTitle = GUI::createSubtitle(section, "连接设备（点击槽位交换 hostId）");
	lv_obj_set_style_text_font(slotTitle, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	auto row = GUI::createFlex(section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(row, 0, 0);
	lv_obj_set_style_pad_column(row, 6, 0);
	lv_obj_set_style_border_width(row, 0, 0);
	lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);

	for (int s = 0; s < SLOT_COUNT; s++)
	{
		auto card = lv_obj_create(row);
		lv_obj_set_flex_grow(card, 1);
		lv_obj_set_height(card, 110);
		lv_obj_set_style_radius(card, 8, 0);
		lv_obj_set_style_bg_color(card, lv_color_hex(C_SLOT_EMPTY), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(card, 2, 0);
		lv_obj_set_style_border_color(card, lv_color_hex(0x444455), 0);
		lv_obj_set_style_pad_all(card, 4, 0);
		lv_obj_set_layout(card, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_user_data(card, (void*)(uintptr_t)s);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(card, onSlotClickCb, LV_EVENT_CLICKED, this);
		// 焦点样式：聚焦时白色 outline
		lv_obj_set_style_outline_width(card, 0, 0);
		lv_obj_set_style_outline_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_outline_color(card, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_outline_pad(card, 2, LV_STATE_FOCUSED);
		m_slotCards[s] = card;

		auto label = lv_label_create(card);
		lv_label_set_text(label, s == 0 ? "本机" : "--");
		lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(label, lv_color_hex(0x888888), 0);
		m_slotLabels[s] = label;

		// 掩码按钮行
		auto maskRow = lv_obj_create(card);
		lv_obj_set_style_border_width(maskRow, 0, 0);
		lv_obj_set_style_bg_opa(maskRow, LV_OPA_TRANSP, 0);
		lv_obj_set_size(maskRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_layout(maskRow, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(maskRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_pad_column(maskRow, 3, 0);
		lv_obj_set_style_pad_all(maskRow, 0, 0);

		for (int p = 0; p < 4; p++)
		{
			auto mb = lv_obj_create(maskRow);
			lv_obj_set_size(mb, 20, 20);
			lv_obj_set_style_radius(mb, LV_RADIUS_CIRCLE, 0);
			lv_obj_set_style_border_width(mb, 2, 0);
			lv_obj_set_style_border_color(mb, lv_color_hex(0x666677), 0);
			lv_obj_set_style_border_color(mb, lv_color_white(), LV_STATE_FOCUSED);
			lv_obj_set_style_border_width(mb, 3, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_width(mb, 0, 0);
			lv_obj_set_style_outline_width(mb, 2, LV_STATE_FOCUSED);
			lv_obj_set_style_outline_color(mb, lv_color_hex(0x4FC3F7), LV_STATE_FOCUSED);
			lv_obj_set_style_outline_pad(mb, 2, LV_STATE_FOCUSED);
			lv_obj_set_style_bg_color(mb, lv_color_hex(C_MASK_OFF), 0);
			lv_obj_set_style_bg_opa(mb, LV_OPA_COVER, 0);
			lv_obj_set_style_pad_all(mb, 0, 0);
			lv_obj_add_flag(mb, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_set_user_data(mb, (void*)(uintptr_t)(s * 4 + p));
			lv_obj_add_event_cb(mb, onMaskClickCb, LV_EVENT_CLICKED, this);
			m_maskBtns[s][p] = mb;

			// 默认 Slot 0 P0-P3 全亮
			if (s == 0) {
				lv_obj_set_style_bg_color(mb, lv_color_hex(C_MASK_ON), 0);
			}
		}

		// 操作按钮行（仅非本机槽位显示）
		auto actionRow = lv_obj_create(card);
		lv_obj_set_style_border_width(actionRow, 0, 0);
		lv_obj_set_style_bg_opa(actionRow, LV_OPA_TRANSP, 0);
		lv_obj_set_size(actionRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_layout(actionRow, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(actionRow, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_pad_column(actionRow, 4, 0);
		lv_obj_set_style_pad_all(actionRow, 0, 0);
		lv_obj_set_style_pad_top(actionRow, 4, 0);

		if (s == 0) {
			// Slot 0 无操作按钮
			m_swapBtns[s] = nullptr;
			m_disconnectBtns[s] = nullptr;
		} else {
			// 交换按钮
			auto swapBtn = lv_btn_create(actionRow);
		lv_obj_set_size(swapBtn, 48, 24);
		lv_obj_set_style_radius(swapBtn, 4, 0);
		lv_obj_set_style_bg_color(swapBtn, LV_COLOR_MAKE(0x33, 0x44, 0x66), 0);
		lv_obj_set_style_bg_opa(swapBtn, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(swapBtn, 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(swapBtn, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_pad_all(swapBtn, 0, 0);
		lv_obj_set_user_data(swapBtn, (void*)(uintptr_t)s);
		lv_obj_add_event_cb(swapBtn, onSlotSwapCb, LV_EVENT_CLICKED, this);
		auto sl = lv_label_create(swapBtn);
		lv_label_set_text(sl, "⇄");
		lv_obj_center(sl);
		lv_obj_set_style_text_font(sl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		m_swapBtns[s] = swapBtn;

		// 断开按钮
		auto discBtn = lv_btn_create(actionRow);
		lv_obj_set_size(discBtn, 48, 24);
		lv_obj_set_style_radius(discBtn, 4, 0);
		lv_obj_set_style_bg_color(discBtn, LV_COLOR_MAKE(0x66, 0x22, 0x22), 0);
		lv_obj_set_style_bg_opa(discBtn, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(discBtn, 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(discBtn, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_pad_all(discBtn, 0, 0);
		lv_obj_set_user_data(discBtn, (void*)(uintptr_t)s);
		lv_obj_add_event_cb(discBtn, onSlotDisconnectCb, LV_EVENT_CLICKED, this);
		auto dl = lv_label_create(discBtn);
		lv_label_set_text(dl, "⛔");
		lv_obj_center(dl);
		lv_obj_set_style_text_font(dl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		m_disconnectBtns[s] = discBtn;
	}
	}
}

// ════════════════════════════════════════════════════════════════
// 扫描 / 连接
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::doScan()
{
	if (!running) {
		if (!deletable) { deletable = true; }
		return;
	}
	if (m_scanActive) { ESP_LOGW(TAG, "扫描中，跳过"); return; }

	m_scanActive = true;
	{ auto g = display->lockGuard(); lv_label_set_text(m_scanBtnLabel, "搜索中..."); }

	DiscoveredConsole buffer[MaxConsoles];
	int count = discoverConsoles(buffer, MaxConsoles, ScanTimeoutMs);

	m_consoles.clear();
	for (int i = 0; i < count; i++) m_consoles.push_back(buffer[i]);
	m_scanActive = false;

	if (!running) { deletable = true; return; }

	{ auto g = display->lockGuard();
		lv_label_set_text(m_scanBtnLabel, "扫描");
		updateScanList();
		updateStatus();
	}
	ESP_LOGI(TAG, "扫描完成，发现 %zu 台主机", m_consoles.size());
}

void ConsoleManagerApp::doConnect(size_t index)
{
	if (index >= m_consoles.size()) { ESP_LOGE(TAG, "无效索引 %zu", index); return; }
	const auto& c = m_consoles[index];
	ESP_LOGI(TAG, "连接到主机: %s @ %s:%u", c.hostname, c.ip, c.port);
	if (HostManager::instance().isClient())
		HostManager::instance().disconnectFromHost();
	HostManager::instance().connectToHost(c.ip, 8080, c.hostname);
}

// ════════════════════════════════════════════════════════════════
// 列表 / 槽位 / 状态更新
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::updateScanList()
{
	auto guard = display->lockGuard();
	for (auto* row : m_scanRows) { if (row) lv_obj_del(row); }
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

		auto leftFlex = lv_obj_create(row);
		lv_obj_set_height(leftFlex, lv_pct(100));
		lv_obj_set_flex_grow(leftFlex, 1);
		lv_obj_remove_flag(leftFlex, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_remove_flag(leftFlex, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_border_width(leftFlex, 0, 0);
		lv_obj_set_style_bg_opa(leftFlex, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(leftFlex, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(leftFlex, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_style_pad_all(leftFlex, 0, 0);

		auto nameLabel = GUI::createLabel(leftFlex, c.hostname);
		lv_obj_set_style_text_font(nameLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		char ipStr[32]; snprintf(ipStr, sizeof(ipStr), "%s:%u", c.ip, c.port);
		auto ipLabel = GUI::createLabel(leftFlex, ipStr);
		lv_obj_set_style_text_font(ipLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(ipLabel, GUI::Color::SUBTLE, 0);

		auto connectBtn = GUI::createButton(row, "连接", 72, 32);
		lv_obj_set_style_radius(connectBtn, 6, 0);
		lv_obj_set_style_bg_color(connectBtn, LV_COLOR_MAKE(0x00, 0x77, 0x00), 0);
		lv_obj_set_user_data(connectBtn, (void*)(uintptr_t)i);
		lv_obj_add_event_cb(connectBtn, onConnectBtnCb, LV_EVENT_CLICKED, this);

		m_scanRows.push_back(row);
		m_connectBtns.push_back(connectBtn);
	}
}

void ConsoleManagerApp::updateSlots()
{
	auto guard = display->lockGuard();
	const auto* slots = HostManager::instance().getSlots();

	for (int s = 0; s < SLOT_COUNT; s++)
	{
		const auto& slot = slots[s];
		auto* card = m_slotCards[s];

		if (slot.connected)
		{
			lv_obj_set_style_bg_color(card,
				s == 0 ? lv_color_hex(C_SLOT_SELF) : lv_color_hex(C_SLOT_OCCUPIED), 0);
			lv_obj_set_style_border_color(card, lv_color_hex(0x444455), 0);
			lv_label_set_text(m_slotLabels[s], slot.hostname[0] ? slot.hostname :
				(s == 0 ? "本机" : "设备"));
		}
		else
		{
			lv_obj_set_style_bg_color(card, lv_color_hex(C_SLOT_EMPTY), 0);
			lv_obj_set_style_border_color(card, lv_color_hex(0x444455), 0);
			lv_label_set_text(m_slotLabels[s], s == 0 ? "本机" : "--");
		}

		// 掩码按钮：读取 VirtualGamepad 的 hostMask
		// 默认 Slot 0 全亮，其余根据 m_hostMask
		// 简化：用 Slot 0 的 mask 表示"本机"，其他槽位按需
	}

	// 交换选中高亮
	for (int s = 0; s < SLOT_COUNT; s++)
	{
		if (m_swapSelected == s)
			lv_obj_set_style_border_color(m_slotCards[s], lv_color_hex(C_SWAP_SEL), 0);
		else
			lv_obj_set_style_border_color(m_slotCards[s], lv_color_hex(0x444455), 0);
	}
}

void ConsoleManagerApp::updateStatus()
{
	auto guard = display->lockGuard();
	if (HostManager::instance().isClient())
	{
		char buf[128];
		snprintf(buf, sizeof(buf), "已连接到 %s (Slot %d)",
			HostManager::instance().getRemoteHostname(),
			HostManager::instance().getMySlot());
		lv_label_set_text(m_connStatusLabel, buf);
		lv_obj_set_style_text_color(m_connStatusLabel, GUI::Color::SUCCESS, 0);
	}
	else
	{
		char buf[128];
		if (m_consoles.empty())
			snprintf(buf, sizeof(buf), "就绪 — 未连接");
		else
			snprintf(buf, sizeof(buf), "发现 %zu 台主机", m_consoles.size());
		lv_label_set_text(m_connStatusLabel, buf);
		lv_obj_set_style_text_color(m_connStatusLabel, GUI::Color::SUBTLE, 0);
	}
}

void ConsoleManagerApp::toggleMask(int slotId, int playerId)
{
	if (slotId < 0 || slotId >= SLOT_COUNT || playerId < 0 || playerId > 3) return;
	auto* mb = m_maskBtns[slotId][playerId];
	if (!mb) return;

	lv_color_t cur = lv_obj_get_style_bg_color(mb, LV_PART_MAIN);
	lv_color_t onColor = lv_color_hex(C_MASK_ON);
	bool isOn = lv_color_eq(cur, onColor);
	lv_obj_set_style_bg_color(mb, lv_color_hex(isOn ? C_MASK_OFF : C_MASK_ON), LV_PART_MAIN);

	uint8_t mask = 0;
	for (int p = 0; p < 4; p++) {
		if (m_maskBtns[slotId][p]) {
			auto c = lv_obj_get_style_bg_color(m_maskBtns[slotId][p], LV_PART_MAIN);
			if (lv_color_eq(c, onColor)) mask |= (1 << p);
		}
	}
	VirtualGamepad::instance().setHostMask(slotId, mask);
}

// ════════════════════════════════════════════════════════════════
// 焦点导航
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::applyFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	auto cf = [](lv_obj_t* o) { if (o) lv_obj_clear_state(o, LV_STATE_FOCUSED); };
	auto fc = [](lv_obj_t* o) { if (o) lv_obj_add_state(o, LV_STATE_FOCUSED); };

	cf(m_backBtn); cf(m_scanBtn);
	for (auto* r : m_scanRows) cf(r);
	for (int s = 0; s < SLOT_COUNT; s++) {
		cf(m_slotCards[s]);
		cf(m_swapBtns[s]);
		cf(m_disconnectBtns[s]);
		for (int p = 0; p < 4; p++) cf(m_maskBtns[s][p]);
	}

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		lv_obj_t* tgt[] = { m_backBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 2) fc(tgt[m_focusTitleIdx]);
		break;
	}
	case FOCUS_LIST:
		if (m_focusListIdx >= 0 && (size_t)m_focusListIdx < m_scanRows.size()) {
			fc(m_scanRows[m_focusListIdx]);
			lv_obj_scroll_to_view(m_scanRows[m_focusListIdx], LV_ANIM_ON);
		}
		break;
	case FOCUS_SLOTS:
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < SLOT_COUNT) {
			fc(m_slotCards[m_focusSlotIdx]);
		}
		break;
	case FOCUS_MASK:
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < SLOT_COUNT) {
			fc(m_slotCards[m_focusSlotIdx]);
			if (m_focusMaskIdx >= 0 && m_focusMaskIdx < 4)
				fc(m_maskBtns[m_focusSlotIdx][m_focusMaskIdx]);
		}
		break;
	case FOCUS_CARD_BTNS:
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < SLOT_COUNT) {
			fc(m_slotCards[m_focusSlotIdx]);
			lv_obj_t* btns[] = { m_swapBtns[m_focusSlotIdx], m_disconnectBtns[m_focusSlotIdx] };
			if (m_focusActionIdx >= 0 && m_focusActionIdx < 2 && btns[m_focusActionIdx])
				fc(btns[m_focusActionIdx]);
		}
		break;
	}
}

void ConsoleManagerApp::activateFocus()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		auto g = display->lockGuard();
		lv_obj_t* tgt[] = { m_backBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 2)
			lv_obj_send_event(tgt[m_focusTitleIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_LIST:
	{
		auto g = display->lockGuard();
		size_t idx = (size_t)m_focusListIdx;
		if (idx < m_connectBtns.size() && m_connectBtns[idx])
			lv_obj_send_event(m_connectBtns[idx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_SLOTS:
	{
		auto g = display->lockGuard();
		if (m_focusSlotIdx < 0 || m_focusSlotIdx >= SLOT_COUNT) break;

		if (m_swapSelected >= 0) {
			// 交换模式：A=完成交换（Slot 0 不可交换）
			if (m_focusSlotIdx == 0) break;
			if (m_swapSelected == m_focusSlotIdx) {
				m_swapSelected = -1;
				updateSlots(); updateStatus();
			} else {
				HostManager::instance().swapSlots(m_swapSelected, m_focusSlotIdx);
				m_swapSelected = -1;
				updateSlots(); updateStatus();
			}
			break;
		}

		// 有连接时进入卡片按钮层
		const auto* slots = HostManager::instance().getSlots();
		if (slots[m_focusSlotIdx].connected) {
			m_focusGroup = FOCUS_CARD_BTNS;
			m_focusActionIdx = 0;
			applyFocus();
		}
		break;
	}
	case FOCUS_MASK:
	{
		auto g = display->lockGuard();
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < SLOT_COUNT &&
			m_focusMaskIdx >= 0 && m_focusMaskIdx < 4) {
			lv_obj_send_event(m_maskBtns[m_focusSlotIdx][m_focusMaskIdx], LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	case FOCUS_CARD_BTNS:
	{
		auto g = display->lockGuard();
		if (m_focusSlotIdx >= 0 && m_focusSlotIdx < SLOT_COUNT) {
			lv_obj_t* btns[] = { m_swapBtns[m_focusSlotIdx], m_disconnectBtns[m_focusSlotIdx] };
			if (m_focusActionIdx >= 0 && m_focusActionIdx < 2 && btns[m_focusActionIdx])
				lv_obj_send_event(btns[m_focusActionIdx], LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	}
}

void ConsoleManagerApp::navLeft()
{
	auto g = display->lockGuard();
	switch (m_focusGroup) {
	case FOCUS_TITLE: if (m_focusTitleIdx > 0) m_focusTitleIdx--; break;
	case FOCUS_SLOTS: if (m_focusSlotIdx > 0) m_focusSlotIdx--; break;
	case FOCUS_MASK:  if (m_focusMaskIdx > 0) m_focusMaskIdx--; break;
	case FOCUS_CARD_BTNS: if (m_focusActionIdx > 0) m_focusActionIdx--; break;
	default: break;
	}
	applyFocus();
}

void ConsoleManagerApp::navRight()
{
	auto g = display->lockGuard();
	switch (m_focusGroup) {
	case FOCUS_TITLE: if (m_focusTitleIdx < 1) m_focusTitleIdx++; break;
	case FOCUS_SLOTS: if (m_focusSlotIdx < SLOT_COUNT - 1) m_focusSlotIdx++; break;
	case FOCUS_MASK:  if (m_focusMaskIdx < 3) m_focusMaskIdx++; break;
	case FOCUS_CARD_BTNS: if (m_focusActionIdx < 1) m_focusActionIdx++; break;
	default: break;
	}
	applyFocus();
}

void ConsoleManagerApp::navUp()
{
	auto g = display->lockGuard();
	switch (m_focusGroup) {
	case FOCUS_LIST:
		if (m_focusListIdx > 0) { m_focusListIdx--; }
		else { m_focusGroup = FOCUS_TITLE; }
		break;
	case FOCUS_SLOTS:
		m_focusGroup = FOCUS_LIST;
		m_focusListIdx = 0;
		break;
	case FOCUS_MASK:
		m_focusGroup = FOCUS_SLOTS;
		m_focusMaskIdx = 0;
		break;
	case FOCUS_CARD_BTNS:
		m_focusGroup = FOCUS_SLOTS;
		m_focusActionIdx = 0;
		break;
	default: break;
	}
	applyFocus();
}

void ConsoleManagerApp::navDown()
{
	auto g = display->lockGuard();
	switch (m_focusGroup) {
	case FOCUS_TITLE:
		if (!m_scanRows.empty()) { m_focusGroup = FOCUS_LIST; m_focusListIdx = 0; }
		break;
	case FOCUS_LIST:
		if ((size_t)m_focusListIdx < m_scanRows.size() - 1) { m_focusListIdx++; }
		else { m_focusGroup = FOCUS_SLOTS; m_focusSlotIdx = 0; }
		break;
	case FOCUS_SLOTS:
		m_focusGroup = FOCUS_MASK;
		m_focusMaskIdx = 0;
		break;
	case FOCUS_MASK:
		m_focusGroup = FOCUS_CARD_BTNS;
		m_focusActionIdx = 0;
		break;
	default: break;
	}
	applyFocus();
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	constexpr uint8_t dz = 13, ctr = 128;
	bool lxL = state.lx < ctr - dz, lxR = state.lx > ctr + dz;
	bool lyU = state.ly < ctr - dz, lyD = state.ly > ctr + dz;

	// ── B: 取消交换 / 退出卡片按钮 / 返回 ──
	if (newPress & (uint16_t)GamepadButton::BTN_B) {
		if (m_nextActionTime < xTaskGetTickCount()) {
			m_nextActionTime = xTaskGetTickCount() + 500;

			if (m_swapSelected >= 0) {
				m_swapSelected = -1;
				if (auto g = display->lockGuard()) { updateSlots(); updateStatus(); }
				return;
			}
			if (m_focusGroup == FOCUS_CARD_BTNS) {
				m_focusGroup = FOCUS_SLOTS;
				applyFocus();
				return;
			}
			if (m_focusGroup == FOCUS_MASK) {
				m_focusGroup = FOCUS_SLOTS;
				m_focusMaskIdx = 0;
				applyFocus();
				return;
			}
			Task::addTask([](void* p)->TickType_t{
				static_cast<ConsoleManagerApp*>(p)->popApp(); return Task::infinityTime;
			}, "consoleBack", this, 0, Task::Affinity::None);
		}
		return;
	}

	// ── A / L3: 激活焦点 ──
	if ((newPress & (uint16_t)GamepadButton::BTN_A) ||
		(newPress & (uint16_t)GamepadButton::BTN_L3)) {
		if (xTaskGetTickCount() < m_nextActionTime) return;
		m_nextActionTime = xTaskGetTickCount() + 500;
		activateFocus();
	}

	// ── 摇杆归位判断 ──
	if (!lxL && !lxR && !lyU && !lyD) { m_nextMoveTime[playerId] = 0; return; }
	if (m_nextMoveTime[playerId] >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	// ── 交换模式：限制只在 4 个槽位间左右导航 ──
	if (m_swapSelected >= 0) {
		if (m_focusGroup != FOCUS_SLOTS) {
			m_focusGroup = FOCUS_SLOTS;
		}
		if (lxL) navLeft();
		if (lxR) navRight();
		return;
	}

	switch (m_focusGroup) {
	case FOCUS_TITLE:
		if (lxL) navLeft();
		if (lxR) navRight();
		if (lyD) navDown();
		break;
	case FOCUS_LIST:
		if (lyU) navUp();
		if (lyD) navDown();
		break;
	case FOCUS_SLOTS:
		if (lxL) navLeft();
		if (lxR) navRight();
		if (lyU) navUp();
		if (lyD) navDown();
		break;
	case FOCUS_MASK:
		if (lxL) navLeft();
		if (lxR) navRight();
		if (lyU) navUp();
		if (lyD) navDown();
		break;
	case FOCUS_CARD_BTNS:
		if (lxL) { m_focusActionIdx = 0; applyFocus(); }
		if (lxR) { m_focusActionIdx = 1; applyFocus(); }
		if (lyU) { m_focusGroup = FOCUS_SLOTS; applyFocus(); }
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onScanBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE; self->m_focusTitleIdx = 1;
	Task::addTask([](void* p)->TickType_t{
		static_cast<ConsoleManagerApp*>(p)->doScan(); return Task::infinityTime;
	}, "consoleScan", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onConnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	size_t idx = (size_t)(uintptr_t)lv_obj_get_user_data(btn);
	self->m_focusGroup = FOCUS_LIST; self->m_focusListIdx = (int8_t)idx;
	Task::addTask([](void* p)->TickType_t{
		auto* pair = static_cast<std::pair<ConsoleManagerApp*, size_t>*>(p);
		pair->first->doConnect(pair->second); delete pair; return Task::infinityTime;
	}, "consoleConnect", new std::pair(self, idx), 0, Task::Affinity::None);
}

void ConsoleManagerApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	Task::addTask([](void* p)->TickType_t{
		static_cast<ConsoleManagerApp*>(p)->popApp(); return Task::infinityTime;
	}, "consoleBack", self, 0, Task::Affinity::None);
}

void ConsoleManagerApp::onSlotClickCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	auto* card = static_cast<lv_obj_t*>(lv_event_get_target(e));
	int slotId = (int)(uintptr_t)lv_obj_get_user_data(card);

	const auto* slots = HostManager::instance().getSlots();
	if (!slots[slotId].connected) return;  // 空槽不可交换

	if (self->m_swapSelected < 0) {
		// 选中第一个
		self->m_swapSelected = (int8_t)slotId;
		lv_obj_set_style_border_color(card, lv_color_hex(C_SWAP_SEL), 0);
	} else if (self->m_swapSelected == slotId) {
		// 点击同一个，取消选中
		self->m_swapSelected = -1;
		// 恢复边框颜色在 updateSlots 中统一处理
		if (auto g = self->display->lockGuard()) self->updateSlots();
	} else {
		// 交换两个槽位
		int a = self->m_swapSelected, b = slotId;
		self->m_swapSelected = -1;
		ESP_LOGI(TAG, "swap slot %d <-> %d", a, b);
		// 交换 HostManager 的槽位映射
		HostManager::instance().swapSlots(a, b);
		if (auto g = self->display->lockGuard()) self->updateSlots();
	}
}

void ConsoleManagerApp::onMaskClickCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));
	auto* mb = static_cast<lv_obj_t*>(lv_event_get_target(e));
	int id = (int)(uintptr_t)lv_obj_get_user_data(mb);
	int slotId = id / 4, playerId = id % 4;
	self->toggleMask(slotId, playerId);
}

void ConsoleManagerApp::onSlotSwapCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));

	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	int slotId = (int)(uintptr_t)lv_obj_get_user_data(btn);
	if (slotId == 0) return;  // Slot 0 不可交换

	if (self->m_swapSelected < 0) {
		self->m_swapSelected = (int8_t)slotId;
		if (auto g = self->display->lockGuard()) self->updateSlots();
		return;
	}

	if (self->m_swapSelected == slotId) {
		self->m_swapSelected = -1;
		if (auto g = self->display->lockGuard()) self->updateSlots();
		return;
	}

	ESP_LOGI(TAG, "swap slot %d <-> %d", self->m_swapSelected, slotId);
	HostManager::instance().swapSlots(self->m_swapSelected, slotId);
	self->m_swapSelected = -1;
	if (auto g = self->display->lockGuard()) { self->updateSlots(); self->updateStatus(); }
}

void ConsoleManagerApp::onSlotDisconnectCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));

	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	int slotId = (int)(uintptr_t)lv_obj_get_user_data(btn);

	if (slotId == 0) return;  // 不能断开自己
	ESP_LOGI(TAG, "disconnect slot %d", slotId);
	// 通过 InputHost 清理
	// 简化：直接断开整个从机连接
	if (HostManager::instance().isClient())
		HostManager::instance().disconnectFromHost();
	if (auto g = self->display->lockGuard()) { self->updateSlots(); self->updateStatus(); }
}

// ════════════════════════════════════════════════════════════════
// AI 能力
// ════════════════════════════════════════════════════════════════

std::string ConsoleManagerApp::getAiContext()
{
	bool isClient = HostManager::instance().isClient();
	std::string ctx = isClient ?
		"当前是从机模式，已连接到 " + std::string(HostManager::instance().getRemoteHostname()) + "\n" :
		"当前是主机模式\n";

	ctx += "可用焦点组：\n"
		"- title(0-1)\t返回/扫描\n"
		"- list(0-" + std::to_string(m_consoles.empty() ? 0 : (int)m_consoles.size() - 1) + ")\t主机列表\n"
		"- slots(0-3)\t选择设备槽位\n"
		"- mask(slot,0-3)\t掩码编辑\n"
		"- actions(slot,0-1)\t交换/断开\n\n";

	ctx += "发现 " + std::to_string(m_consoles.size()) + " 台主机\n";

	const char* gn = "";
	switch (m_focusGroup) {
	case FOCUS_TITLE: gn = "title"; break;
	case FOCUS_LIST:  gn = "list"; break;
	case FOCUS_SLOTS: gn = "slots"; break;
	case FOCUS_MASK:  gn = "mask"; break;
	case FOCUS_CARD_BTNS: gn = "cardBtns"; break;
	}
	ctx += std::string("焦点@") + gn + "\n";
	return ctx;
}

void ConsoleManagerApp::onAiSelect(const AiSelectTarget& target)
{
	auto g = display->lockGuard(); if (!g) return;
	if (target.group == "title" && target.index >= 0 && target.index <= 1) {
		m_focusGroup = FOCUS_TITLE; m_focusTitleIdx = (int8_t)target.index;
	} else if (target.group == "list" && target.index >= 0 && (size_t)target.index < m_scanRows.size()) {
		m_focusGroup = FOCUS_LIST; m_focusListIdx = (int8_t)target.index;
	} else if (target.group == "slots" && target.index >= 0 && target.index < SLOT_COUNT) {
		m_focusGroup = FOCUS_SLOTS; m_focusSlotIdx = (int8_t)target.index;
	} else if (target.group == "mask" && target.index >= 0 && target.index < SLOT_COUNT) {
		m_focusGroup = FOCUS_MASK; m_focusSlotIdx = (int8_t)target.index; m_focusMaskIdx = 0;
	} else if (target.group == "cardBtns" && target.index >= 0 && target.index < SLOT_COUNT) {
		m_focusGroup = FOCUS_CARD_BTNS; m_focusSlotIdx = (int8_t)target.index; m_focusActionIdx = 0;
	}
	applyFocus();
}

void ConsoleManagerApp::onAiActivate() { activateFocus(); }
