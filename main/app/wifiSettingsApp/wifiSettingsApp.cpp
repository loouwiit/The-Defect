#include "wifiSettingsApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

WifiSettingsApp::WifiSettingsApp(Display* display)
	: App(display)
{
}

WifiSettingsApp::~WifiSettingsApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::init()
{
	App::init();

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

	// 初始聚焦
	m_focusGroup = FOCUS_TITLE;
	m_focusTitleIdx = 0;
	m_focusListIdx = 0;
	applyFocus();

	// 同步初始 WiFi 开关状态
	{
		if (wifiStationIsStarted())
		{
			lv_label_set_text(m_wifiToggleLabel, "WiFi: ON");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
		}
		else
		{
			lv_label_set_text(m_wifiToggleLabel, "WiFi: OFF");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
		}
	}

	// 创建定时器
	m_refreshTimer = lv_timer_create(timerCb, RefreshInterval, this);

	ESP_LOGI(TAG, "WiFi 设置 App 初始化完成");
}

void WifiSettingsApp::deinit()
{
	if (m_refreshTimer)
	{
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}
	// 关闭密码弹窗
	hidePasswordDialog();

	App::deinit();
	ESP_LOGI(TAG, "WiFi 设置 App 已释放");
}

void WifiSettingsApp::onForeground()
{
	if (m_refreshTimer)
		lv_timer_resume(m_refreshTimer);
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + 500;
	ESP_LOGI(TAG, "前台");
}

void WifiSettingsApp::onBackground()
{
	if (m_refreshTimer)
		lv_timer_pause(m_refreshTimer);
	ESP_LOGI(TAG, "后台");
}

// ════════════════════════════════════════════════════════════════
// 定时器回调
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::timerCb(lv_timer_t* t)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_timer_get_user_data(t));
	self->refreshUi();
}

void WifiSettingsApp::refreshUi()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	// ── 同步 WiFi 开关状态 ──
	{
		if (wifiStationIsStarted())
		{
			lv_label_set_text(m_wifiToggleLabel, "WiFi: ON");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
		}
		else
		{
			lv_label_set_text(m_wifiToggleLabel, "WiFi: OFF");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
		}
	}

	// ── 密码弹窗打开时，跳过扫描列表更新（避免干扰 textarea 光标） ──
	if (m_passwordDialog)
	{
		updateConnectionInfo();
		return;
	}

	// ── 扫描状态检查（仅 WiFi 开启时） ──
	if (wifiStationIsStarted())
	{
		if (m_scanActive)
		{
			// 从 WiFi 驱动同步最新结果（持续合并直到超时）
			uint16_t count = 0;
			esp_err_t err = esp_wifi_scan_get_ap_num(&count);
			if (err == ESP_OK && count > 0)
			{
				auto* apInfo = new wifi_ap_record_t[MaxAps]();
				uint16_t got = count;
				if (got > MaxAps) got = MaxAps;
				if (esp_wifi_scan_get_ap_records(&got, apInfo) == ESP_OK)
				{
					// 合并新结果到 m_scanResults（不删除已有数据）
					for (uint16_t i = 0; i < got; i++)
					{
						const char* ssid = reinterpret_cast<const char*>(apInfo[i].ssid);
						if (ssid[0] == '\0') continue;

						// 查找是否已有同名 AP，保留 RSSI 更大的
						bool found = false;
						for (auto& existing : m_scanResults)
						{
							if (strcmp(reinterpret_cast<const char*>(existing.ssid), ssid) == 0)
							{
								if (apInfo[i].rssi > existing.rssi)
									existing = apInfo[i];
								found = true;
								break;
							}
						}
						if (!found)
							m_scanResults.push_back(apInfo[i]);
					}
				}
				delete[] apInfo;
			}

			updateScanList();
			applyFocus();

			// 检查超时
			if (xTaskGetTickCount() - m_scanStartTick >= pdMS_TO_TICKS(ScanDuration))
			{
				ESP_LOGI(TAG, "扫描超时 %dms，自动停止", ScanDuration);
				esp_wifi_scan_stop();
				m_scanActive = false;
				lv_label_set_text(m_scanBtnLabel, "扫描");
			}
		}
		else
		{
			// 冻结列表，只更新连接状态
			updateScanList();
			applyFocus();
		}
	}
	else
	{
		// WiFi 关闭，不更新扫描列表
		if (m_scanActive)
		{
			m_scanActive = false;
			esp_wifi_scan_stop();
		}
	}

	// ── 更新连接信息 ──
	updateConnectionInfo();
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::buildUi()
{
	// 屏幕设为 flex 列布局
	lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 顶部栏：返回 + 标题 + 扫描按钮 ──
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

	auto title = GUI::createTitle(top_bar, "WiFi 设置");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// WiFi 开关按钮
	m_wifiToggleBtn = GUI::createButton(top_bar, "WiFi: ON", 150, 40);
	m_wifiToggleLabel = lv_obj_get_child(m_wifiToggleBtn, 0);
	lv_obj_set_style_border_width(m_wifiToggleBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_wifiToggleBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_wifiToggleBtn, onWifiToggleCb, LV_EVENT_CLICKED, this);

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

	auto scan_title = GUI::createSubtitle(scan_section, "附近的 WiFi");
	lv_obj_set_style_text_font(scan_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

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
	auto empty_hint = GUI::createLabel(m_scanListContainer, "点击“扫描”搜索附近的 WiFi");
	lv_obj_set_style_text_color(empty_hint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(empty_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(empty_hint, 20, 0);
	lv_obj_set_width(empty_hint, lv_pct(100));
	lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
	m_scanRows.push_back(empty_hint);

	// ── 底部连接信息区域 ──
	m_infoCard = lv_obj_create(screen);
	lv_obj_set_width(m_infoCard, lv_pct(100));
	lv_obj_set_height(m_infoCard, LV_SIZE_CONTENT);
	styleCard(m_infoCard);
	lv_obj_set_layout(m_infoCard, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_infoCard, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(m_infoCard, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(m_infoCard, 12, 0);
	lv_obj_set_style_pad_left(m_infoCard, 16, 0);
	lv_obj_set_style_pad_right(m_infoCard, 16, 0);
	lv_obj_set_style_border_width(m_infoCard, 0, 0);
	lv_obj_set_style_bg_opa(m_infoCard, LV_OPA_COVER, 0);

	// 左侧图标 + 文本
	auto info_left = lv_obj_create(m_infoCard);
	lv_obj_set_height(info_left, lv_pct(100));
	lv_obj_set_flex_grow(info_left, 1);
	lv_obj_remove_flag(info_left, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_remove_flag(info_left, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_style_border_width(info_left, 0, 0);
	lv_obj_set_style_bg_opa(info_left, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(info_left, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(info_left, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(info_left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
	lv_obj_set_style_pad_all(info_left, 0, 0);

	m_infoIcon = GUI::createLabel(info_left, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_font(m_infoIcon, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_text_color(m_infoIcon, GUI::Color::SUBTLE, 0);

	m_infoSsidLabel = GUI::createLabel(info_left, "未连接");
	lv_obj_set_style_text_font(m_infoSsidLabel, FontLoader::getDefault(FontLoader::FontSize::Default), 0);

	m_infoIpLabel = GUI::createLabel(info_left, "");
	lv_obj_set_style_text_font(m_infoIpLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_infoIpLabel, GUI::Color::SUBTLE, 0);

	// 右侧断开按钮
	m_disconnectBtn = GUI::createButton(m_infoCard, "断开", 80, 36);
	lv_obj_set_style_bg_color(m_disconnectBtn, GUI::Color::DANGER, 0);
	lv_obj_set_style_radius(m_disconnectBtn, 6, 0);
	lv_obj_set_style_pad_all(m_disconnectBtn, 0, 0);
	lv_obj_set_style_border_width(m_disconnectBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_disconnectBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_disconnectBtn, onDisconnectBtnCb, LV_EVENT_CLICKED, this);
	lv_obj_add_flag(m_disconnectBtn, LV_OBJ_FLAG_HIDDEN);
}

// ════════════════════════════════════════════════════════════════
// 扫描
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::doScan()
{
	if (m_scanActive)
	{
		// 已激活扫描，停止
		m_scanActive = false;
		esp_wifi_scan_stop();
		if (auto guard = display->lockGuard())
			lv_label_set_text(m_scanBtnLabel, "扫描");
		ESP_LOGI(TAG, "扫描已停止");
		return;
	}

	if (!wifiStationIsStarted() || !wifiApIsStarted() || !wifiNatIsStarted())
	{
		ESP_LOGW(TAG, "未启动,自动启动");
		wifiStationStart();
		wifiApStart();
		wifiNatStart();
		{
			auto guard = display->lockGuard();
			lv_label_set_text(m_wifiToggleLabel, "WiFi: ON");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
		}
	}

	// 清空旧结果
	m_scanResults.clear();
	m_focusGroup = FOCUS_LIST;
	m_focusListIdx = 0;

	m_scanActive = true;
	m_scanStartTick = xTaskGetTickCount();
	{
		auto guard = display->lockGuard();
		lv_label_set_text(m_scanBtnLabel, "停止");
	}

	// 非阻塞启动扫描（结果在 refreshUi 定时器中轮询获取）
	wifi_scan_config_t scan{};
	memset(&scan, 0, sizeof(scan));
	scan.show_hidden = false;
	esp_wifi_scan_start(&scan, false);

	ESP_LOGI(TAG, "扫描已启动（%dms 后自动停止）", ScanDuration);
}

// ════════════════════════════════════════════════════════════════
// 扫描列表更新
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::updateScanList()
{
	// 删除所有旧行
	for (auto* row : m_scanRows)
	{
		if (row) lv_obj_del(row);
	}
	m_scanRows.clear();
	m_connectBtns.clear();

	if (!wifiStationIsStarted())
	{
		auto hint = GUI::createLabel(m_scanListContainer, "WiFi 已关闭，请点击上方“WiFi: OFF”开启");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_pad_top(hint, 20, 0);
		lv_obj_set_width(hint, lv_pct(100));
		lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
		m_scanRows.push_back(hint);
		return;
	}

	if (m_scanResults.empty())
	{
		auto hint = GUI::createLabel(m_scanListContainer,
			m_scanActive ? "正在搜索 WiFi..." : "未发现 WiFi 热点");
		lv_obj_set_style_text_color(hint, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_pad_top(hint, 20, 0);
		lv_obj_set_width(hint, lv_pct(100));
		lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
		m_scanRows.push_back(hint);
		return;
	}

	// 获取当前连接的 SSID（用于标记"当前网络"）
	wifi_sta_config_t currentConfig{};
	char currentSsid[33]{};
	if (wifiIsConnect())
	{
		currentConfig = wifiStationGetInfo();
		memcpy(currentSsid, currentConfig.ssid, sizeof(currentSsid));
		currentSsid[32] = '\0';
	}

	for (size_t i = 0; i < m_scanResults.size(); i++)
	{
		const auto& ap = m_scanResults[i];
		const char* apSsid = reinterpret_cast<const char*>(ap.ssid);

		// 跳过隐藏 SSID
		if (apSsid[0] == '\0') continue;

		bool isCurrentNetwork = (wifiIsConnect() && strcmp(apSsid, currentSsid) == 0);

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
			auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
			auto* r = static_cast<lv_obj_t*>(lv_event_get_target(e));
			self->m_focusGroup = FOCUS_LIST;
			self->m_focusListIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(r);
			self->applyFocus();
			}, LV_EVENT_CLICKED, this);
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(row, 8, 0);
		lv_obj_set_style_pad_left(row, 12, 0);
		lv_obj_set_style_pad_right(row, 8, 0);

		// 左侧：SSID + RSSI
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

		auto name_label = GUI::createLabel(left_flex, apSsid);
		lv_obj_set_style_text_font(name_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_flex_grow(name_label, 1);
		lv_obj_remove_flag(name_label, LV_OBJ_FLAG_SCROLLABLE);
		lv_label_set_long_mode(name_label, LV_LABEL_LONG_DOT);

		char rssiStr[16];
		snprintf(rssiStr, sizeof(rssiStr), "%d dBm", ap.rssi);
		auto rssi_label = GUI::createLabel(left_flex, rssiStr);
		lv_obj_set_style_text_font(rssi_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(rssi_label, GUI::Color::SUBTLE, 0);
		lv_obj_remove_flag(rssi_label, LV_OBJ_FLAG_SCROLLABLE);

		// 右侧：状态/操作按钮
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

		if (isCurrentNetwork)
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

		m_scanRows.push_back(row);
	}
}

// ════════════════════════════════════════════════════════════════
// 连接信息更新
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::updateConnectionInfo()
{
	if (!wifiStationIsStarted())
	{
		lv_label_set_text(m_infoSsidLabel, "WiFi 未开启");
		lv_label_set_text(m_infoIpLabel, "");
		lv_obj_add_flag(m_disconnectBtn, LV_OBJ_FLAG_HIDDEN);
		m_isConnected = false;
		return;
	}

	bool connected = wifiIsConnect();

	if (connected)
	{
		auto config = wifiStationGetInfo();
		auto ip = wifiStationGetIp();

		char ssidStr[33];
		memcpy(ssidStr, config.ssid, sizeof(ssidStr));
		ssidStr[32] = '\0';
		lv_label_set_text(m_infoSsidLabel, ssidStr);

		char ipStr[20];
		snprintf(ipStr, sizeof(ipStr), IPSTR, IP2STR(&ip));
		lv_label_set_text(m_infoIpLabel, ipStr);

		lv_obj_clear_flag(m_disconnectBtn, LV_OBJ_FLAG_HIDDEN);
		m_isConnected = true;
	}
	else
	{
		lv_label_set_text(m_infoSsidLabel, "未连接");
		lv_label_set_text(m_infoIpLabel, "");
		lv_obj_add_flag(m_disconnectBtn, LV_OBJ_FLAG_HIDDEN);
		m_isConnected = false;
	}
}

// ════════════════════════════════════════════════════════════════
// 操作
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::doConnect(size_t scanIndex, const char* password)
{
	if (scanIndex >= m_scanResults.size())
	{
		ESP_LOGE(TAG, "无效的扫描索引 %zu", scanIndex);
		return;
	}

	const auto& ap = m_scanResults[scanIndex];
	const char* apSsid = reinterpret_cast<const char*>(ap.ssid);

	ESP_LOGI(TAG, "正在连接 %s", apSsid);
	wifiConnect(apSsid, password);

	// 隐藏密码弹窗
	hidePasswordDialog();

	// 更新扫描列表显示
	refreshUi();
}

void WifiSettingsApp::doDisconnect()
{
	ESP_LOGI(TAG, "断开 WiFi");
	wifiDisconnect();
	refreshUi();
}

// ════════════════════════════════════════════════════════════════
// WiFi 开关
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::toggleWifi()
{
	if (wifiStationIsStarted())
	{
		// 关闭 WiFi
		if (m_scanActive)
		{
			m_scanActive = false;
			esp_wifi_scan_stop();
		}
		if (wifiIsConnect())
			wifiDisconnect();
		wifiStationStop();
		{
			auto guard = display->lockGuard();
			lv_label_set_text(m_wifiToggleLabel, "WiFi: OFF");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
		}
		ESP_LOGI(TAG, "WiFi 已关闭");
	}
	else
	{
		// 开启 WiFi
		wifiStationStart();
		{
			auto guard = display->lockGuard();
			lv_label_set_text(m_wifiToggleLabel, "WiFi: ON");
			lv_obj_set_style_bg_color(m_wifiToggleBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
		}
		ESP_LOGI(TAG, "WiFi 已开启");
	}
}

// ════════════════════════════════════════════════════════════════
// 密码弹窗
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::showPasswordDialog(size_t scanIndex)
{
	if (m_passwordDialog) return;
	if (scanIndex >= m_scanResults.size()) return;

	m_pwdTargetIdx = scanIndex;
	const auto& ap = m_scanResults[scanIndex];
	const char* apSsid = reinterpret_cast<const char*>(ap.ssid);

	// 创建遮罩
	// m_passwordDialog = lv_obj_create(screen);
	// lv_obj_set_size(m_passwordDialog, lv_pct(100), lv_pct(100));
	// lv_obj_set_style_bg_color(m_passwordDialog, lv_color_hex(0x000000), 0);
	// lv_obj_set_style_bg_opa(m_passwordDialog, LV_OPA_60, 0);
	// lv_obj_set_style_border_width(m_passwordDialog, 0, 0);
	// lv_obj_set_style_pad_all(m_passwordDialog, 0, 0);
	// lv_obj_clear_flag(m_passwordDialog, LV_OBJ_FLAG_SCROLLABLE);
	// lv_obj_move_foreground(m_passwordDialog);

	// 居中卡片（从上到下排列，确保按钮可见）
	auto card = lv_obj_create(screen);
	lv_obj_set_size(card, lv_pct(100), lv_pct(100));
	lv_obj_center(card);
	styleCard(card);
	lv_obj_set_layout(card, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(card, 12, 0);
	lv_obj_set_style_pad_top(card, 20, 0);

	// 标题
	auto title = GUI::createLabel(card, "输入 WiFi 密码");
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Default), 0);

	// SSID 副标题
	char subStr[64];
	snprintf(subStr, sizeof(subStr), "SSID: %s", apSsid);
	auto sub = GUI::createLabel(card, subStr);
	lv_obj_set_style_text_font(sub, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(sub, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_pad_bottom(sub, 8, 0);

	// 密码输入框
	m_passwordTa = lv_textarea_create(card);
	lv_obj_set_width(m_passwordTa, lv_pct(90));
	lv_obj_set_height(m_passwordTa, 60);
	lv_textarea_set_password_mode(m_passwordTa, true);
	lv_textarea_set_placeholder_text(m_passwordTa, "请输入密码");
	lv_obj_set_style_text_align(m_passwordTa, LV_TEXT_ALIGN_LEFT, 0);

	// 键盘（固定高度，避免 flex 重算导致光标晃动）
	m_keyboard = lv_keyboard_create(card);
	lv_obj_set_width(m_keyboard, lv_pct(100));
	lv_obj_set_height(m_keyboard, 280);
	lv_obj_set_flex_grow(m_keyboard, 0);
	lv_keyboard_set_textarea(m_keyboard, m_passwordTa);

	// 按钮行
	auto btn_row = lv_obj_create(card);
	lv_obj_set_width(btn_row, lv_pct(90));
	lv_obj_set_height(btn_row, LV_SIZE_CONTENT);
	lv_obj_remove_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(btn_row, 0, 0);
	lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
	lv_obj_set_layout(btn_row, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(btn_row, 8, 0);

	auto cancelBtn = GUI::createButton(btn_row, "取消", 120, 44);
	lv_obj_set_style_bg_color(cancelBtn, LV_COLOR_MAKE(0x50, 0x50, 0x60), 0);
	lv_obj_add_event_cb(cancelBtn, onPwdCancelCb, LV_EVENT_CLICKED, this);

	auto confirmBtn = GUI::createButton(btn_row, "连接", 120, 44);
	lv_obj_set_style_bg_color(confirmBtn, LV_COLOR_MAKE(0x00, 0x88, 0x00), 0);
	lv_obj_add_event_cb(confirmBtn, onPwdConfirmCb, LV_EVENT_CLICKED, this);
}

void WifiSettingsApp::hidePasswordDialog()
{
	if (!m_passwordDialog) return;
	m_keyboard = nullptr;
	m_passwordTa = nullptr;
	lv_obj_del(m_passwordDialog);
	m_passwordDialog = nullptr;
}

// ════════════════════════════════════════════════════════════════
// 焦点导航
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::applyFocus()
{
	auto clearFocus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clearFocus(m_backBtn);
	clearFocus(m_wifiToggleBtn);
	clearFocus(m_scanBtn);
	clearFocus(m_disconnectBtn);
	for (auto* row : m_scanRows) clearFocus(row);

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		lv_obj_t* targets[] = { m_backBtn, m_wifiToggleBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 3)
			focus(targets[m_focusTitleIdx]);
		break;
	}
	break;
	case FOCUS_LIST:
		if (m_focusListIdx >= 0 && (size_t)m_focusListIdx < m_scanRows.size())
		{
			focus(m_scanRows[m_focusListIdx]);
			lv_obj_scroll_to_view(m_scanRows[m_focusListIdx], LV_ANIM_ON);
		}
		break;
	case FOCUS_INFO:
		focus(m_disconnectBtn);
		break;
	}
}

void WifiSettingsApp::activateFocus()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
	{
		auto guard = display->lockGuard();
		lv_obj_t* targets[] = { m_backBtn, m_wifiToggleBtn, m_scanBtn };
		if (m_focusTitleIdx >= 0 && m_focusTitleIdx < 3)
			lv_obj_send_event(targets[m_focusTitleIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_LIST:
	{
		size_t idx = (size_t)m_focusListIdx;
		if (idx < m_connectBtns.size() && m_connectBtns[idx])
		{
			lv_obj_send_event(m_connectBtns[idx], LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	case FOCUS_INFO:
	{
		if (m_isConnected)
		{
			auto guard = display->lockGuard();
			lv_obj_send_event(m_disconnectBtn, LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	}
}

// ── 标题行导航 ──

void WifiSettingsApp::navTitleLeft()
{
	auto guard = display->lockGuard();
	if (m_focusTitleIdx > 0) m_focusTitleIdx--;
	applyFocus();
}

void WifiSettingsApp::navTitleRight()
{
	auto guard = display->lockGuard();
	if (m_focusTitleIdx < 2) m_focusTitleIdx++;
	applyFocus();
}

// ── 列表导航 ──

void WifiSettingsApp::navListUp()
{
	auto guard = display->lockGuard();
	if (m_focusListIdx > 0)
	{
		m_focusListIdx--;
	}
	else
	{
		m_focusGroup = FOCUS_TITLE;
	}
	applyFocus();
}

void WifiSettingsApp::navListDown()
{
	auto guard = display->lockGuard();
	int8_t last = (int8_t)m_scanRows.size() - 1;
	if (m_focusListIdx < last)
	{
		m_focusListIdx++;
	}
	else
	{
		m_focusGroup = FOCUS_INFO;
	}
	applyFocus();
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 边沿检测 ──
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// ── 密码弹窗中：B 键取消 ──
	if (m_passwordDialog)
	{
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
		{
			hidePasswordDialog();
			if (auto guard = display->lockGuard())
				applyFocus();
		}
		return;
	}

	// ── B：返回 ──
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + 500;
			// LVGL 事件回调中持锁，栈操作须延后
			Task::addTask([](void* param) -> TickType_t
				{
					static_cast<WifiSettingsApp*>(param)->popApp();
					return Task::infinityTime;
				}, "wifiBack", this, 0, Task::Affinity::None);
		}
		return;
	}

	// ── A / L3：激活焦点 ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
			activateFocus();
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
			if (m_scanRows.empty())
				m_focusGroup = FOCUS_INFO;
			else
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

	case FOCUS_INFO:
		if (lyUp)
		{
			auto g = display->lockGuard();
			if (!m_scanRows.empty())
			{
				m_focusGroup = FOCUS_LIST;
				m_focusListIdx = (int8_t)m_scanRows.size() - 1;
			}
			else
				m_focusGroup = FOCUS_TITLE;
			applyFocus();
		}
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::onWifiToggleCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;
	Task::addTask([](void* param) -> TickType_t
		{
			auto& self = *static_cast<WifiSettingsApp*>(param);
			self.toggleWifi();
			return Task::infinityTime;
		}, "wifiToggle", self, 0, Task::Affinity::None);
}

void WifiSettingsApp::onScanBtnCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 1;
	Task::addTask([](void* param) -> TickType_t
		{
			auto& self = *static_cast<WifiSettingsApp*>(param);
			self.doScan();
			return Task::infinityTime;
		}, "wifiToggleScan", self, 0, Task::Affinity::None);
}

void WifiSettingsApp::onConnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	size_t scanIndex = (size_t)lv_obj_get_user_data(btn);

	self->m_focusGroup = FOCUS_LIST;
	self->m_focusListIdx = (int8_t)scanIndex;

	self->showPasswordDialog(scanIndex);
}

void WifiSettingsApp::onDisconnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_INFO;
	self->doDisconnect();
}

void WifiSettingsApp::onPwdConfirmCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	if (!self->m_passwordTa) return;

	const char* password = lv_textarea_get_text(self->m_passwordTa);
	self->doConnect(self->m_pwdTargetIdx, password);
}

void WifiSettingsApp::onPwdCancelCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->hidePasswordDialog();
	if (auto guard = self->display->lockGuard())
		self->applyFocus();
}

void WifiSettingsApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;
	self->m_focusTitleIdx = 0;

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	// LVGL 事件回调中持锁，栈操作须延后
	Task::addTask([](void* param) -> TickType_t
		{
			static_cast<WifiSettingsApp*>(param)->popApp();
			return Task::infinityTime;
		}, "wifiBack", self, 0, Task::Affinity::None);
}
