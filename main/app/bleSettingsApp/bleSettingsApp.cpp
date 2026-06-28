#include "bleSettingsApp.hpp"
#include "app/desktopApp/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "bleGamepad/bleGamepad.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

BleSettingsApp::BleSettingsApp(Display* display)
	: App(display)
{
}

BleSettingsApp::~BleSettingsApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::init()
{
	App::init();

	ESP_LOGI(TAG, "address = %p", this);

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}

	// 页面背景
	lv_obj_set_style_bg_color(screen, GUI::Color::BG, 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	buildUi();

	// 停止后台扫描，进入手动模式
	BleGamepad::instance().stopScan();
	m_localScanResults.clear();
	m_scanActive = false;

	// 初始聚焦
	m_focusGroup = FOCUS_TITLE;
	m_focusTitleIdx = 0;
	m_focusListIdx = 0;
	m_focusSlotsIdx = 0;
	applyFocus();

	ESP_LOGI(TAG, "BLE 设置 App 初始化完成");
}

void BleSettingsApp::deinit()
{
	// 删除定时器
	if (m_refreshTimer)
	{
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}

	App::deinit();
	ESP_LOGI(TAG, "BLE 设置 App 已释放");
}

void BleSettingsApp::onForeground()
{
	// 创建或恢复定时器
	if (!m_refreshTimer)
	{
		m_refreshTimer = lv_timer_create(timerCb, RefreshInterval, this);
	}
	else
	{
		lv_timer_resume(m_refreshTimer);
	}
	ESP_LOGI(TAG, "前台，定时器已启动");
}

void BleSettingsApp::onBackground()
{
	// 暂停定时器
	if (m_refreshTimer)
	{
		lv_timer_pause(m_refreshTimer);
	}

	// 如果正在扫描则停止，不恢复后台扫描
	if (m_scanActive)
	{
		m_scanActive = false;
		BleGamepad::instance().stopScan();
	}

	ESP_LOGI(TAG, "后台，定时器已暂停");
}

void BleSettingsApp::onGamepadConnected(uint8_t playerId)
{
	m_pendingRefresh = true;
	ESP_LOGI(TAG, "手柄 %d 已连接（待刷新）", playerId);
}

void BleSettingsApp::onGamepadDisconnected(uint8_t playerId)
{
	m_pendingRefresh = true;
	ESP_LOGI(TAG, "手柄 %d 已断开（待刷新）", playerId);
}

// ════════════════════════════════════════════════════════════════
// 定时器回调
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::timerCb(lv_timer_t* t)
{
	auto* self = static_cast<BleSettingsApp*>(lv_timer_get_user_data(t));
	self->refreshUi();
}

void BleSettingsApp::refreshUi()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	// ── 扫描状态检查 ──
	if (m_scanActive)
	{
		// 更新扫描指示
		// 从 BleGamepad 同步最新结果
		auto devices = BleGamepad::instance().getScannedDevices();
		m_localScanResults = devices;
		updateScanList();
		applyFocus();

		// 检查 5 秒超时
		if (xTaskGetTickCount() - m_scanStartTick >= pdMS_TO_TICKS(ScanDuration))
		{
			ESP_LOGI(TAG, "扫描超时 %dms，自动停止", ScanDuration);
			toggleScan();  // 会 stopScan + 更新按钮
		}
	}
	else
	{		// 冻结列表，只更新已连接状态
		updateScanList();
		applyFocus();
	}

	// ── 已连接区域 ──
	updateConnectedList();

	m_pendingRefresh = false;
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::buildUi()
{
	// 屏幕设为 flex 列布局
	lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 顶部栏：返回 + 标题 ──
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

	auto title = GUI::createTitle(top_bar, "蓝牙手柄设置");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// 扫描按钮 + 指示（右上角）
	m_scanBtn = GUI::createButton(top_bar, "扫描", 100, 40);
	m_scanBtnLabel = lv_obj_get_child(m_scanBtn, 0);
	lv_obj_set_style_border_width(m_scanBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_scanBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_scanBtn, onScanBtnCb, LV_EVENT_CLICKED, this);



	// ── 扫描结果列表（flex_grow 填充中间剩余空间） ──
	auto scan_section = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(scan_section, 1);
	lv_obj_set_style_pad_all(scan_section, 8, 0);
	lv_obj_set_style_pad_left(scan_section, 16, 0);
	lv_obj_set_style_pad_right(scan_section, 16, 0);
	lv_obj_set_style_border_width(scan_section, 0, 0);
	lv_obj_set_style_bg_opa(scan_section, LV_OPA_TRANSP, 0);

	auto scan_title = GUI::createSubtitle(scan_section, "附近的设备");
	lv_obj_set_style_text_font(scan_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	// 可滚动的列表容器（flex_grow 填充 scan_section 剩余空间）
	m_scanListContainer = lv_obj_create(scan_section);
	lv_obj_set_width(m_scanListContainer, lv_pct(100));
	lv_obj_set_flex_grow(m_scanListContainer, 1);
	lv_obj_set_style_border_width(m_scanListContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_scanListContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_scanListContainer, 0, 0);
	lv_obj_set_layout(m_scanListContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_scanListContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollbar_mode(m_scanListContainer, LV_SCROLLBAR_MODE_AUTO);

	// 初始空状态提示
	auto empty_hint = GUI::createLabel(m_scanListContainer, "点击“扫描”搜索附近的蓝牙手柄");
	lv_obj_set_style_text_color(empty_hint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(empty_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(empty_hint, 20, 0);
	lv_obj_set_width(empty_hint, lv_pct(100));
	lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);

	m_scanRows.push_back(empty_hint);

	// ── 已连接手柄区域（固定在 flex 底部） ──
	m_connectedContainer = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(m_connectedContainer, 8, 0);
	lv_obj_set_style_pad_left(m_connectedContainer, 16, 0);
	lv_obj_set_style_pad_right(m_connectedContainer, 16, 0);
	lv_obj_set_style_pad_bottom(m_connectedContainer, 24, 0);
	lv_obj_set_style_border_width(m_connectedContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_connectedContainer, LV_OPA_TRANSP, 0);

	auto connected_title = GUI::createSubtitle(m_connectedContainer, "已连接手柄");
	lv_obj_set_style_text_font(connected_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	// 4 个玩家槽位 — 一行排列
	auto slots_row = lv_obj_create(m_connectedContainer);
	lv_obj_set_width(slots_row, lv_pct(100));
	lv_obj_set_height(slots_row, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(slots_row, 0, 0);
	lv_obj_set_style_bg_opa(slots_row, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(slots_row, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(slots_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(slots_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(slots_row, 0, 0);
	lv_obj_set_style_pad_column(slots_row, 8, 0);
	lv_obj_remove_flag(slots_row, LV_OBJ_FLAG_SCROLLABLE);

	for (int i = 0; i < MaxPlayers; i++)
	{
		auto card = lv_obj_create(slots_row);
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
		lv_obj_set_style_pad_left(card, 8, 0);
		lv_obj_set_style_pad_right(card, 8, 0);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_user_data(card, (void*)(uintptr_t)i);
		lv_obj_add_event_cb(card, [](lv_event_t* e) {
			auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));
			auto* c = static_cast<lv_obj_t*>(lv_event_get_target(e));
			self->m_focusGroup = FOCUS_SLOTS;
			self->m_focusSlotsIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(c);
			self->applyFocus();
		}, LV_EVENT_CLICKED, this);
		m_slotCards[i] = card;

		// 第一行：名称
		char slotText[16];
		snprintf(slotText, sizeof(slotText), "P%d: (空)", i + 1);
		auto label = GUI::createLabel(card, slotText);
		lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_width(label, lv_pct(100));
		lv_obj_set_flex_grow(label, 1);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
		m_slotLabels[i] = label;

		// 第二行：RSSI + 断开按钮
		auto bottom_row = lv_obj_create(card);
		lv_obj_set_width(bottom_row, lv_pct(100));
		lv_obj_set_height(bottom_row, LV_SIZE_CONTENT);
		lv_obj_set_style_border_width(bottom_row, 0, 0);
		lv_obj_set_style_bg_opa(bottom_row, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(bottom_row, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(bottom_row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(bottom_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(bottom_row, 0, 0);
		lv_obj_remove_flag(bottom_row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_remove_flag(bottom_row, LV_OBJ_FLAG_CLICKABLE);

		auto rssiLabel = GUI::createLabel(bottom_row, "");
		lv_obj_set_style_text_font(rssiLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(rssiLabel, GUI::Color::SUBTLE, 0);
		m_slotRssiLabels[i] = rssiLabel;

		auto disconnectBtn = GUI::createButton(bottom_row, "断开", 56, 24);
		lv_obj_set_style_bg_color(disconnectBtn, GUI::Color::DANGER, 0);
		lv_obj_set_style_radius(disconnectBtn, 6, 0);
		lv_obj_set_style_pad_all(disconnectBtn, 0, 0);
		lv_obj_add_event_cb(disconnectBtn, onDisconnectBtnCb, LV_EVENT_CLICKED, this);
		lv_obj_set_user_data(disconnectBtn, (void*)(uintptr_t)i);
		lv_obj_add_flag(disconnectBtn, LV_OBJ_FLAG_HIDDEN);
		m_disconnectBtns[i] = disconnectBtn;

		m_slotConnected[i] = false;
	}
}

// ════════════════════════════════════════════════════════════════
// 扫描列表更新
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::updateScanList()
{
	// 删除所有旧行
	for (auto* row : m_scanRows)
	{
		if (row) lv_obj_del(row);
	}
	m_scanRows.clear();
	m_connectBtns.clear();

	if (m_localScanResults.empty())
	{
		auto hint = GUI::createLabel(m_scanListContainer,
			m_scanActive ? "正在搜索设备..." : "未发现设备");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_pad_top(hint, 20, 0);
		lv_obj_set_width(hint, lv_pct(100));
		lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
		m_scanRows.push_back(hint);
		return;
	}

	// 构建设备行
	for (size_t i = 0; i < m_localScanResults.size(); i++)
	{
		const auto& dev = m_localScanResults[i];

		// 检查是否已连接（在任一玩家槽位中）
		bool isConnected = false;
		for (uint8_t p = 0; p < MaxPlayers; p++)
		{
			auto* ctx = BleGamepad::instance().getDevice(p);
			if (ctx && ctx->connected && memcmp(ctx->bda, dev.bda, 6) == 0)
			{
				isConnected = true;
				break;
			}
		}

		auto row = lv_obj_create(m_scanListContainer);
		lv_obj_set_width(row, lv_pct(100));
		lv_obj_set_height(row, RowHight);
		styleCard(row);
		lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_border_width(row, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(row, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_layout(row, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
		lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_user_data(row, (void*)(uintptr_t)i);
		lv_obj_add_event_cb(row, [](lv_event_t* e) {
			auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));
			auto* r = static_cast<lv_obj_t*>(lv_event_get_target(e));
			self->m_focusGroup = FOCUS_LIST;
			self->m_focusListIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(r);
			self->applyFocus();
		}, LV_EVENT_CLICKED, this);
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(row, 8, 0);
		lv_obj_set_style_pad_left(row, 12, 0);
		lv_obj_set_style_pad_right(row, 8, 0);

		// 左侧：名称 + RSSI
		auto left_flex = lv_obj_create(row);
		lv_obj_set_height(left_flex, lv_pct(100));
		lv_obj_set_flex_grow(left_flex, 1);
		lv_obj_remove_flag(left_flex, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_remove_flag(left_flex, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_border_width(left_flex, 0, 0);
		lv_obj_set_style_bg_opa(left_flex, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(left_flex, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(left_flex, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(left_flex, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(left_flex, 0, 0);
		lv_obj_set_style_pad_right(left_flex, 8, 0);

		auto name_label = GUI::createLabel(left_flex, dev.name);
		lv_obj_set_style_text_font(name_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_flex_grow(name_label, 1);
		lv_obj_remove_flag(name_label, LV_OBJ_FLAG_SCROLLABLE);
		lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

		char rssiStr[16];
		snprintf(rssiStr, sizeof(rssiStr), "%d dBm", dev.rssi);
		auto rssi_label = GUI::createLabel(left_flex, rssiStr);
		lv_obj_set_style_text_font(rssi_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(rssi_label, GUI::Color::SUBTLE, 0);
		lv_obj_remove_flag(rssi_label, LV_OBJ_FLAG_SCROLLABLE);

		// 右侧：操作按钮
		auto right_flex = lv_obj_create(row);
		lv_obj_set_height(right_flex, lv_pct(100));
		lv_obj_remove_flag(right_flex, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_remove_flag(right_flex, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_border_width(right_flex, 0, 0);
		lv_obj_set_style_bg_opa(right_flex, LV_OPA_TRANSP, 0);
		lv_obj_set_layout(right_flex, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(right_flex, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(right_flex, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(right_flex, 0, 0);

		if (isConnected)
		{
			auto statusLabel = GUI::createLabel(right_flex, "已连接");
			lv_obj_set_style_text_color(statusLabel, GUI::Color::SUCCESS, 0);
			lv_obj_set_style_text_font(statusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
			m_connectBtns.push_back(nullptr);
		}
		else
		{
			auto connectBtn = GUI::createButton(right_flex, "连接", 72, 32);
			lv_obj_set_style_radius(connectBtn, 6, 0);
			lv_obj_set_user_data(connectBtn, (void*)(uintptr_t)i);
			lv_obj_add_event_cb(connectBtn, onConnectBtnCb, LV_EVENT_CLICKED, this);
			m_connectBtns.push_back(connectBtn);
		}

		// 删除按钮（仅未连接时显示）
		if (!isConnected)
		{
			auto delBtn = GUI::createButton(right_flex, "×", 36, 32);
			lv_obj_set_style_bg_color(delBtn, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
			lv_obj_set_style_radius(delBtn, 6, 0);
			lv_obj_set_style_pad_all(delBtn, 0, 0);
			lv_obj_set_user_data(delBtn, (void*)(uintptr_t)i);
			lv_obj_add_event_cb(delBtn, onDeleteBtnCb, LV_EVENT_CLICKED, this);
		}

		m_scanRows.push_back(row);
	}
}

// ════════════════════════════════════════════════════════════════
// 已连接列表更新
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::updateConnectedList()
{
	for (int i = 0; i < MaxPlayers; i++)
	{
		auto* ctx = BleGamepad::instance().getDevice(i);

		if (ctx && ctx->connected)
		{
			char slotText[64];
			snprintf(slotText, sizeof(slotText), "P%d: %s", i + 1, ctx->name);
			lv_label_set_text(m_slotLabels[i], slotText);

			// 从本地扫描结果查找 RSSI
			int8_t rssi = 0;
			for (auto& dev : m_localScanResults)
			{
				if (memcmp(dev.bda, ctx->bda, 6) == 0)
				{
					rssi = dev.rssi;
					break;
				}
			}
			char rssiStr[16];
			if (rssi != 0)
				snprintf(rssiStr, sizeof(rssiStr), "%d dBm", rssi);
			else
				snprintf(rssiStr, sizeof(rssiStr), "已连接");
			lv_label_set_text(m_slotRssiLabels[i], rssiStr);

			lv_obj_clear_flag(m_disconnectBtns[i], LV_OBJ_FLAG_HIDDEN);
			m_slotConnected[i] = true;
		}
		else
		{
			char slotText[16];
			snprintf(slotText, sizeof(slotText), "P%d: (空)", i + 1);
			lv_label_set_text(m_slotLabels[i], slotText);
			lv_label_set_text(m_slotRssiLabels[i], "");
			lv_obj_add_flag(m_disconnectBtns[i], LV_OBJ_FLAG_HIDDEN);
			m_slotConnected[i] = false;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 操作
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::toggleScan()
{
	if (m_scanActive)
	{		// 停止扫描
		m_scanActive = false;
		BleGamepad::instance().stopScan();
		{
			auto guard = display->lockGuard();
			lv_label_set_text(m_scanBtnLabel, "扫描");
		}
		ESP_LOGI(TAG, "扫描已停止，列表冻结 (%zu 台设备)", m_localScanResults.size());
	}
	else
	{		// 开始扫描
		m_localScanResults.clear();
		m_focusGroup = FOCUS_TITLE;
		m_focusTitleIdx = 1;
		m_focusListIdx = 0;
		m_focusSlotsIdx = 0;
		m_scanActive = true;
		m_scanStartTick = xTaskGetTickCount();
		BleGamepad::instance().startScan();

		{
			auto guard = display->lockGuard();
			lv_label_set_text(m_scanBtnLabel, "停止");
		}

		ESP_LOGI(TAG, "开始扫描（%dms 后自动停止）", ScanDuration);
	}
}

void BleSettingsApp::doConnect(size_t scanIndex)
{
	if (scanIndex >= m_localScanResults.size())
	{
		ESP_LOGE(TAG, "无效的扫描索引 %zu", scanIndex);
		return;
	}

	ESP_LOGI(TAG, "正在连接设备: %s", m_localScanResults[scanIndex].name);
	BleGamepad::instance().connect(static_cast<uint8_t>(scanIndex));
	// 注意：实际的扫描索引在 BleGamepad 中对应的是动态列表，
	// 但我们的 m_localScanResults 在冻结后与扫描结果快照一致
}

void BleSettingsApp::doDisconnect(uint8_t playerId)
{
	if (playerId >= MaxPlayers)
	{
		ESP_LOGE(TAG, "无效的玩家 ID %u", playerId);
		return;
	}

	auto* ctx = BleGamepad::instance().getDevice(playerId);
	if (!ctx || !ctx->connected)
	{
		ESP_LOGW(TAG, "玩家 %u 未连接", playerId);
		return;
	}

	ESP_LOGI(TAG, "断开玩家 %u: %s", playerId, ctx->name);

	// 将设备加回扫描列表（以便重新连接）
	bool found = false;
	for (auto& dev : m_localScanResults)
	{
		if (memcmp(dev.bda, ctx->bda, 6) == 0)
		{
			found = true;
			break;
		}
	}
	if (!found)
	{
		ScanDevice dev;
		memcpy(dev.bda, ctx->bda, 6);
		snprintf(dev.name, sizeof(dev.name), "%s", ctx->name);
		m_localScanResults.push_back(dev);
	}

	BleGamepad::instance().disconnect(playerId);
}

void BleSettingsApp::doDelete(size_t scanIndex)
{
	if (scanIndex >= m_localScanResults.size())
	{
		ESP_LOGE(TAG, "无效的删除索引 %zu", scanIndex);
		return;
	}

	const auto& dev = m_localScanResults[scanIndex];

	// 如果设备已连接，先断开
	for (uint8_t p = 0; p < MaxPlayers; p++)
	{
		auto* ctx = BleGamepad::instance().getDevice(p);
		if (ctx && ctx->connected && memcmp(ctx->bda, dev.bda, 6) == 0)
		{
			ESP_LOGI(TAG, "删除前先断开玩家 %u: %s", p, ctx->name);
			BleGamepad::instance().disconnect(p);
			break;
		}
	}

	ESP_LOGI(TAG, "从列表移除设备: %s", dev.name);
	m_localScanResults.erase(m_localScanResults.begin() + scanIndex);
}

// ════════════════════════════════════════════════════════════════
// 手柄控制
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::applyFocus()
{
	// 清除所有对象的 LV_STATE_FOCUSED
	auto clearFocus = [](lv_obj_t* obj)
		{		if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED);
		};

	clearFocus(m_backBtn);
	clearFocus(m_scanBtn);
	for (auto* row : m_scanRows) clearFocus(row);
	for (auto* card : m_slotCards) clearFocus(card);

	// 设置当前聚焦对象的 LV_STATE_FOCUSED
	auto focus = [](lv_obj_t* obj)
		{		if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED);
		};

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		focus(m_focusTitleIdx == 0 ? m_backBtn : m_scanBtn);
		break;
	case FOCUS_LIST:
		if (m_focusListIdx >= 0 && (size_t)m_focusListIdx < m_scanRows.size()) {
			focus(m_scanRows[m_focusListIdx]);
			lv_obj_scroll_to_view(m_scanRows[m_focusListIdx], LV_ANIM_ON);
		}
		break;
	case FOCUS_SLOTS:
		if (m_focusSlotsIdx >= 0 && m_focusSlotsIdx < MaxPlayers)
			focus(m_slotCards[m_focusSlotsIdx]);
		break;
	}
}

void BleSettingsApp::activateFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		lv_obj_send_event(m_focusTitleIdx == 0 ? m_backBtn : m_scanBtn,
			LV_EVENT_CLICKED, nullptr);
		break;

	case FOCUS_LIST:
	{
		size_t idx = (size_t)m_focusListIdx;
		if (idx < m_connectBtns.size() && m_connectBtns[idx])
			lv_obj_send_event(m_connectBtns[idx], LV_EVENT_CLICKED, nullptr);
		break;
	}

	case FOCUS_SLOTS:
		if (m_focusSlotsIdx >= 0 && m_focusSlotsIdx < MaxPlayers)
			lv_obj_send_event(m_disconnectBtns[m_focusSlotsIdx],
				LV_EVENT_CLICKED, nullptr);
		break;
	}
}

// ── 标题行导航（左右切换 返回 ↔ 扫描） ──

void BleSettingsApp::navTitleLeft()
{
	auto guard = display->lockGuard();
	m_focusTitleIdx = 0;
	applyFocus();
}

void BleSettingsApp::navTitleRight()
{
	auto guard = display->lockGuard();
	m_focusTitleIdx = 1;
	applyFocus();
}

// ── 设备列表导航 ──

void BleSettingsApp::navListUp()
{
	auto guard = display->lockGuard();
	if (m_focusListIdx > 0)
	{
		m_focusListIdx--;
	}
	else
	{
		m_focusGroup = FOCUS_TITLE;  // 保留 m_focusTitleIdx
	}
	applyFocus();
}

void BleSettingsApp::navListDown()
{
	auto guard = display->lockGuard();
	int8_t last = (int8_t)m_localScanResults.size() - 1;
	if (m_focusListIdx < last)
	{
		m_focusListIdx++;
	}
	else
	{
		m_focusGroup = FOCUS_SLOTS;  // 保留 m_focusSlotsIdx
	}
	applyFocus();
}

void BleSettingsApp::navListHome()
{
	auto guard = display->lockGuard();
	m_focusListIdx = 0;
	applyFocus();
}

void BleSettingsApp::navListEnd()
{
	auto guard = display->lockGuard();
	m_focusListIdx = (int8_t)m_localScanResults.size() - 1;
	if (m_focusListIdx < 0) m_focusListIdx = 0;
	applyFocus();
}

// ── 连接区导航（左右切换槽位） ──

void BleSettingsApp::navSlotsLeft()
{
	auto guard = display->lockGuard();
	if (m_focusSlotsIdx > 0)
	{
		m_focusSlotsIdx--;
	}
	applyFocus();
}

void BleSettingsApp::navSlotsRight()
{
	auto guard = display->lockGuard();
	if (m_focusSlotsIdx < MaxPlayers - 1)
	{
		m_focusSlotsIdx++;
	}
	applyFocus();
}

// ── 主手柄输入 ──

void BleSettingsApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 返回 ──
	if (state.isPressed(GamepadButton::BTN_B))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + 500;
			popApp();
		}
		return;
	}

	// ── 激活 ──
	if (state.isPressed(GamepadButton::BTN_A) || state.isPressed(GamepadButton::BTN_L3))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + 500;
			activateFocus();
		}
		return;
	}

	// ── 摇杆归位判断 ──
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
			m_focusGroup = FOCUS_LIST;
			navListHome();
		}
		break;

	case FOCUS_LIST:
		if (lyUp)    navListUp();
		if (lyDown)  navListDown();
		if (lxLeft)  navListHome();
		if (lxRight) navListEnd();
		break;

	case FOCUS_SLOTS:
		if (lxLeft)  navSlotsLeft();
		if (lxRight) navSlotsRight();
		if (lyUp)
		{
			auto g = display->lockGuard();
			m_focusGroup = FOCUS_LIST;
			navListEnd();
		}
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void BleSettingsApp::onScanBtnCb(lv_event_t* e)
{
	auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 1;
	Task::addTask([](void* param)
		{
			auto& self = *static_cast<BleSettingsApp*>(param);
			self.toggleScan();
			return Task::infinityTime;
		}, "toggleScan", self);
}

void BleSettingsApp::onConnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	size_t scanIndex = (size_t)lv_obj_get_user_data(btn);

	self->m_focusGroup = FOCUS_LIST;
	self->m_focusListIdx = (int8_t)scanIndex;

	self->doConnect(scanIndex);
}

void BleSettingsApp::onDisconnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	uint8_t playerId = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);

	self->m_focusGroup = FOCUS_SLOTS;
	self->m_focusSlotsIdx = (int8_t)playerId;

	self->doDisconnect(playerId);
}

void BleSettingsApp::onDeleteBtnCb(lv_event_t* e)
{
	auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	size_t scanIndex = (size_t)lv_obj_get_user_data(btn);

	// 删除后聚焦到同位置（如果有设备移上来）或前一个
	self->m_focusGroup = FOCUS_LIST;
	self->m_focusListIdx = (int8_t)scanIndex;

	self->doDelete(scanIndex);
}

void BleSettingsApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<BleSettingsApp*>(lv_event_get_user_data(e));

	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 0;

	// LVGL 事件回调中持锁，栈操作须延后
	Task::addTask([](void* param) -> TickType_t
		{
			auto* app = static_cast<BleSettingsApp*>(param);
			if (app->getManager())
			{
				app->popApp();
			}
			return Task::infinityTime;
		}, "bleSettingsBack", self, 0, Task::Affinity::None);
}
