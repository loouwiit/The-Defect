#include "consoleManagerApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "host/hostManager.hpp"
#include "discovery/discovery.hpp"
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

	// ── 底部状态 ──
	m_statusLabel = GUI::createLabel(screen, "就绪");
	lv_obj_set_width(m_statusLabel, lv_pct(100));
	lv_obj_set_style_text_align(m_statusLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_font(m_statusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_pad_all(m_statusLabel, 8, 0);
	lv_obj_set_style_pad_bottom(m_statusLabel, 12, 0);
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

	// 如果已是从机，先断开旧连接
	if (HostManager::instance().isClient())
		HostManager::instance().disconnectFromHost();

	// 通过 HostManager 建立正式连接
	// 内部自动创建 /ws/console 连接 → register → 收到 slot 分配
	HostManager::instance().connectToHost(c.ip, 8080, c.hostname);
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
			auto g = display->lockGuard();
			if (!m_scanRows.empty())
			{
				m_focusGroup = FOCUS_LIST;
				m_focusListIdx = 0;
			}
			applyFocus();
		}
		break;

	case FOCUS_LIST:
		if (lyUp)   navListUp();
		if (lyDown) navListDown();
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void ConsoleManagerApp::onScanBtnCb(lv_event_t* e)
{
	auto* self = static_cast<ConsoleManagerApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
		return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

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

	if (xTaskGetTickCount() < self->m_nextActionTime)
		return;
	self->m_nextActionTime = xTaskGetTickCount() + 500;

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
	}
	ctx += std::string("焦点@") + groupName;
	if (m_focusGroup == FOCUS_TITLE) ctx += ":" + std::to_string(m_focusTitleIdx);
	else if (m_focusGroup == FOCUS_LIST) ctx += ":" + std::to_string(m_focusListIdx);
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
