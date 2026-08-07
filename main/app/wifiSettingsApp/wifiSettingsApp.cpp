#include "wifiSettingsApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "wifi/mdns.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <utility>

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
	m_focusApIdx = 0;
	m_focusStaTitleIdx = 0;
	m_focusListIdx = 0;
	m_focusInfoIdx = 0;
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

	// 同步初始 AP 开关状态
	{
		if (wifiApIsStarted())
		{
			lv_label_set_text(m_apToggleLabel, "AP: ON");
			lv_obj_set_style_bg_color(m_apToggleBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
		}
		else
		{
			lv_label_set_text(m_apToggleLabel, "AP: OFF");
			lv_obj_set_style_bg_color(m_apToggleBtn, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
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
	// 关闭所有弹窗
	hideEditDialog();

	App::deinit();
	ESP_LOGI(TAG, "WiFi 设置 App 已释放");
}

void WifiSettingsApp::onForeground()
{
	if (m_refreshTimer)
		lv_timer_resume(m_refreshTimer);
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
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

	// ── 同步 AP 面板（开关/SSID/密码/认证/客户端/NAT/网关/主机名） ──
	updateApPanel();

	// ── 编辑弹窗打开时，跳过扫描列表更新（避免干扰 textarea 光标） ──
	if (m_editDialog)
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

			// 检查超时
			if (xTaskGetTickCount() - m_scanStartTick >= pdMS_TO_TICKS(ScanDuration))
			{
				ESP_LOGI(TAG, "扫描超时 %dms，自动停止", ScanDuration);
				esp_wifi_scan_stop();
				m_scanActive = false;
				lv_label_set_text(m_scanBtnLabel, "扫描");
				// 扫描结束，重建列表更新提示文本（"正在搜索..." → "未发现" 或保留结果）
				updateScanList();
			}
		}
		// else: 非扫描状态 → 冻结列表，不重建不聚焦，避免对抗触摸滚动
	}
	else
	{
		// WiFi 关闭，更新列表显示提示信息
		if (m_scanActive)
		{
			m_scanActive = false;
			esp_wifi_scan_stop();
		}
		updateScanList();
	}

	// ── 更新连接信息 ──
	updateConnectionInfo();
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::buildUi()
{
	// 屏幕设为 flex 列布局（顶栏 + 内容区）
	lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 顶栏：返回 + 标题 ──
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

	// ── 内容区：左右分栏 ──
	auto content = lv_obj_create(screen);
	lv_obj_set_width(content, lv_pct(100));
	lv_obj_set_flex_grow(content, 1);
	lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(content, 0, 0);
	lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(content, 0, 0);
	lv_obj_set_layout(content, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(content, LV_FLEX_FLOW_ROW);

	// ══════════════════════════════════════════════════════
	// 左列：AP 热点面板
	// ══════════════════════════════════════════════════════
	auto leftCol = lv_obj_create(content);
	lv_obj_set_width(leftCol, lv_pct(50));
	lv_obj_set_height(leftCol, lv_pct(100));
	lv_obj_remove_flag(leftCol, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(leftCol, 0, 0);
	lv_obj_set_style_bg_opa(leftCol, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(leftCol, 8, 0);
	lv_obj_set_style_pad_left(leftCol, 16, 0);
	lv_obj_set_style_pad_right(leftCol, 8, 0);
	lv_obj_set_layout(leftCol, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(leftCol, LV_FLEX_FLOW_COLUMN);

	// 左列头部：标题 + AP 开关
	auto ap_header = GUI::createFlex(leftCol, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(ap_header, 0, 0);
	lv_obj_set_style_bg_opa(ap_header, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(ap_header, 0, 0);
	auto ap_title = GUI::createSubtitle(ap_header, "AP 热点");
	lv_obj_set_style_text_font(ap_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_flex_grow(ap_title, 1);

	m_apToggleBtn = GUI::createButton(ap_header, "AP: ON", 130, 40);
	m_apToggleLabel = lv_obj_get_child(m_apToggleBtn, 0);
	lv_obj_set_style_border_width(m_apToggleBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_apToggleBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_apToggleBtn, onApToggleCb, LV_EVENT_CLICKED, this);

	// AP 行工厂（label 左、value 右、可附带动作按钮）
	auto makeApRow = [&](const char* label) -> lv_obj_t* {
		auto row = lv_obj_create(leftCol);
		lv_obj_set_width(row, lv_pct(100));
		lv_obj_set_height(row, RowHight);
		styleCard(row);
		lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_layout(row, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
		lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(row, 8, 0);
		lv_obj_set_style_pad_left(row, 16, 0);
		lv_obj_set_style_pad_right(row, 16, 0);
		auto lab = GUI::createLabel(row, label);
		lv_obj_set_style_text_font(lab, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		return row;
	};
	// value label：居中生长、右对齐、超长省略
	auto makeValue = [&](lv_obj_t* row, const char* init) -> lv_obj_t* {
		auto val = GUI::createLabel(row, init);
		lv_obj_set_style_text_font(val, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_flex_grow(val, 1);
		lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_set_style_pad_right(val, 8, 0);
		lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
		return val;
	};
	// 可聚焦动作行：label + value + 动作按钮，整行与按钮都响应 cb
	auto makeApActionRow = [&](const char* label, const char* btnText, lv_event_cb_t cb, lv_obj_t** valueOut) -> lv_obj_t* {
		auto row = makeApRow(label);
		auto val = makeValue(row, "-");
		if (valueOut) *valueOut = val;
		auto btn = GUI::createButton(row, btnText, 60, 32);
		lv_obj_set_style_radius(btn, 6, 0);
		lv_obj_set_style_pad_all(btn, 0, 0);
		lv_obj_set_style_bg_color(btn, LV_COLOR_MAKE(0x30, 0x50, 0x70), 0);
		lv_obj_set_style_text_font(lv_obj_get_child(btn, 0),
			FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
		// 仅按钮响应触摸；行仅作显示与手柄焦点载体（不注册行级触摸回调）
		lv_obj_set_style_border_width(row, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(row, lv_color_white(), LV_STATE_FOCUSED);
		return row;
	};
	// 只读信息行：label + value
	auto makeApInfoRow = [&](const char* label, lv_obj_t** valueOut) {
		auto row = makeApRow(label);
		auto val = makeValue(row, "-");
		if (valueOut) *valueOut = val;
	};

	auto ssidRow = makeApActionRow("SSID", "编辑", onApSsidCb, &m_apSsidLabel);
	auto pwdRow  = makeApActionRow("密码", "编辑", onApPwdCb, &m_apPwdLabel);
	auto authRow = makeApActionRow("认证", "切换", onApAuthCb, &m_apAuthLabel);
	makeApInfoRow("客户端", &m_apClientLabel);
	auto natRow  = makeApActionRow("NAT(自动)", "切换", onApNatCb, &m_apNatLabel);
	makeApInfoRow("网关", &m_apGatewayLabel);
	auto hostRow = makeApActionRow("主机名", "编辑", onHostnameRowCb, &m_hostnameLabel);

	// 焦点列表：0=AP 开关, 1=SSID, 2=密码, 3=认证, 4=NAT, 5=主机名
	m_apRows.clear();
	m_apRows.push_back(m_apToggleBtn);
	m_apRows.push_back(ssidRow);
	m_apRows.push_back(pwdRow);
	m_apRows.push_back(authRow);
	m_apRows.push_back(natRow);
	m_apRows.push_back(hostRow);

	updateApPanel();

	// ══════════════════════════════════════════════════════
	// 右列：Station 面板
	// ══════════════════════════════════════════════════════
	auto rightCol = lv_obj_create(content);
	lv_obj_set_width(rightCol, lv_pct(50));
	lv_obj_set_height(rightCol, lv_pct(100));
	lv_obj_remove_flag(rightCol, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_border_width(rightCol, 0, 0);
	lv_obj_set_style_bg_opa(rightCol, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(rightCol, 8, 0);
	lv_obj_set_style_pad_left(rightCol, 8, 0);
	lv_obj_set_style_pad_right(rightCol, 16, 0);
	lv_obj_set_layout(rightCol, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(rightCol, LV_FLEX_FLOW_COLUMN);

	// 右列头部：标题 + WiFi 开关 + 扫描
	auto sta_header = GUI::createFlex(rightCol, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(sta_header, 0, 0);
	lv_obj_set_style_bg_opa(sta_header, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(sta_header, 0, 0);
	auto sta_title = GUI::createSubtitle(sta_header, "WiFi (Station)");
	lv_obj_set_style_text_font(sta_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_flex_grow(sta_title, 1);

	m_wifiToggleBtn = GUI::createButton(sta_header, "WiFi: ON", 130, 40);
	m_wifiToggleLabel = lv_obj_get_child(m_wifiToggleBtn, 0);
	lv_obj_set_style_border_width(m_wifiToggleBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_wifiToggleBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_wifiToggleBtn, onWifiToggleCb, LV_EVENT_CLICKED, this);

	m_scanBtn = GUI::createButton(sta_header, "扫描", 90, 40);
	m_scanBtnLabel = lv_obj_get_child(m_scanBtn, 0);
	lv_obj_set_style_border_width(m_scanBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_scanBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_scanBtn, onScanBtnCb, LV_EVENT_CLICKED, this);

	// 扫描结果列表（flex_grow 填充剩余空间）
	m_scanListContainer = lv_obj_create(rightCol);
	lv_obj_set_width(m_scanListContainer, lv_pct(100));
	lv_obj_set_flex_grow(m_scanListContainer, 1);
	lv_obj_set_style_border_width(m_scanListContainer, 0, 0);
	lv_obj_set_style_bg_opa(m_scanListContainer, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(m_scanListContainer, 0, 0);
	lv_obj_set_layout(m_scanListContainer, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_scanListContainer, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_scrollbar_mode(m_scanListContainer, LV_SCROLLBAR_MODE_AUTO);

	// 触摸滚动结束时同步焦点索引，使手柄导航从正确位置开始
	lv_obj_add_event_cb(m_scanListContainer, [](lv_event_t* e) {
		auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
		if (self->m_scanRows.empty()) return;

		// 找到第一个在可视区域内的行，更新焦点索引
		lv_area_t cont_area;
		lv_obj_get_coords(self->m_scanListContainer, &cont_area);

		for (size_t i = 0; i < self->m_scanRows.size(); i++) {
			lv_area_t row_area;
			lv_obj_get_coords(self->m_scanRows[i], &row_area);
			// 行顶部在可视区域内 → 设为新焦点索引
			if (row_area.y1 >= cont_area.y1 && row_area.y1 <= cont_area.y2) {
				if ((int8_t)i != self->m_focusListIdx) {
					self->m_focusListIdx = (int8_t)i;
					self->m_focusGroup = FOCUS_STA_LIST;
					// 不调用 applyFocus()，避免反向对抗触摸滚动
				}
				break;
			}
		}
		}, LV_EVENT_SCROLL_END, this);

	// 初始空状态提示
	auto empty_hint = GUI::createLabel(m_scanListContainer, "点击“扫描”搜索附近的 WiFi");
	lv_obj_set_style_text_color(empty_hint, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(empty_hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_pad_top(empty_hint, 20, 0);
	lv_obj_set_width(empty_hint, lv_pct(100));
	lv_obj_set_style_text_align(empty_hint, LV_TEXT_ALIGN_CENTER, 0);
	m_scanRows.push_back(empty_hint);

	// ── 底部连接信息区域 ──
	m_infoCard = lv_obj_create(rightCol);
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

	// 扫描需要 STA 模式。分屏并行管理：不静默启动任何东西（含 AP/NAT）
	if (!wifiStationIsStarted())
	{
		ESP_LOGW(TAG, "WiFi(STA) 未开启，无法扫描；请先点击“WiFi: ON”");
		// 列表显示提示（updateScanList 已处理 STA 关闭态）
		if (auto guard = display->lockGuard())
			updateScanList();
		return;
	}

	// 清空旧结果
	m_scanResults.clear();
	m_focusGroup = FOCUS_STA_LIST;
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
			self->m_focusGroup = FOCUS_STA_LIST;
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

void WifiSettingsApp::updateHostnameRow()
{
	if (!m_hostnameLabel) return;
	lv_label_set_text(m_hostnameLabel, mdnsGetHostname());
}

// ════════════════════════════════════════════════════════════════
// AP 面板
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::updateApPanel()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	// AP 开关
	if (wifiApIsStarted())
	{
		lv_label_set_text(m_apToggleLabel, "AP: ON");
		lv_obj_set_style_bg_color(m_apToggleBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
	}
	else
	{
		lv_label_set_text(m_apToggleLabel, "AP: OFF");
		lv_obj_set_style_bg_color(m_apToggleBtn, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
	}

	// SSID
	wifi_ap_config_t cfg = wifiApGetInfo();
	const char* ssid = reinterpret_cast<const char*>(cfg.ssid);
	lv_label_set_text(m_apSsidLabel, ssid[0] ? ssid : "-");

	// 密码（固定掩码，不泄露明文）
	lv_label_set_text(m_apPwdLabel, cfg.password[0] ? "••••••••" : "-");

	// 认证模式
	lv_label_set_text(m_apAuthLabel, authModeName(cfg.authmode));

	// 已连接客户端数（纯计数，标准只读 API，不改驱动）
	if (wifiApIsStarted())
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%d/4", wifiApGetStaCount());
		lv_label_set_text(m_apClientLabel, buf);
	}
	else
	{
		lv_label_set_text(m_apClientLabel, "AP 未开启");
	}

	// NAT 自动启动标志（不显示当前运行态）
	lv_label_set_text(m_apNatLabel, wifiNatIsAutoStart() ? "开" : "关");

	// 网关
	lv_label_set_text(m_apGatewayLabel, "192.168.4.1");

	updateHostnameRow();
}

const char* WifiSettingsApp::authModeName(wifi_auth_mode_t mode)
{
	switch (mode)
	{
	case WIFI_AUTH_OPEN:            return "开放 (OPEN)";
	case WIFI_AUTH_WPA_PSK:         return "WPA";
	case WIFI_AUTH_WPA2_PSK:        return "WPA2";
	case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
	case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-企业";
	case WIFI_AUTH_WPA3_PSK:        return "WPA3";
	case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
	case WIFI_AUTH_WAPI_PSK:        return "WAPI";
	default:                        return "未知";
	}
}

void WifiSettingsApp::toggleAp()
{
	if (wifiApIsStarted())
	{
		wifiApStop();
		ESP_LOGI(TAG, "AP 已关闭");
	}
	else
	{
		wifiApStart();
		ESP_LOGI(TAG, "AP 已开启");
	}
	if (auto guard = display->lockGuard())
		updateApPanel();
}

void WifiSettingsApp::cycleAuthMode()
{
	wifi_ap_config_t cfg = wifiApGetInfo();
	wifi_auth_mode_t next = cfg.authmode;
	switch (next)
	{
	case WIFI_AUTH_OPEN:      next = WIFI_AUTH_WPA2_PSK; break;
	case WIFI_AUTH_WPA3_PSK:  next = WIFI_AUTH_OPEN; break;
	default:                  next = WIFI_AUTH_WPA3_PSK; break;
	}
	wifiApSet(reinterpret_cast<const char*>(cfg.ssid),
		reinterpret_cast<const char*>(cfg.password), next);
	ESP_LOGI(TAG, "AP 认证模式 → %s", authModeName(next));

	if (auto guard = display->lockGuard())
		updateApPanel();
}

void WifiSettingsApp::toggleNat()
{
	if (wifiNatIsAutoStart())
	{
		wifiNatSetAutoStart(false);
		if (wifiNatIsStarted()) wifiNatStop();
		ESP_LOGI(TAG, "NAT 自动启动已关闭");
	}
	else
	{
		wifiNatSetAutoStart(true);
		// STA + AP 均就绪时立即生效，否则由 wifiNatAutoStartDetecte 兜底
		if (wifiIsConnect() && wifiApIsStarted())
			wifiNatStart();
		ESP_LOGI(TAG, "NAT 自动启动已开启");
	}
	if (auto guard = display->lockGuard())
		updateApPanel();
}

void WifiSettingsApp::doSetApSsid(const char* ssid)
{
	if (!ssid || ssid[0] == '\0')
	{
		ESP_LOGW(TAG, "AP SSID 为空，忽略");
		if (auto guard = display->lockGuard())
			updateApPanel();
		return;
	}
	wifi_ap_config_t cfg = wifiApGetInfo();
	wifiApSet(ssid, reinterpret_cast<const char*>(cfg.password), cfg.authmode);
	ESP_LOGI(TAG, "AP SSID → %s", ssid);
	if (auto guard = display->lockGuard())
		updateApPanel();
}

void WifiSettingsApp::doSetApPassword(const char* password)
{
	size_t len = password ? strlen(password) : 0;
	wifi_ap_config_t cfg = wifiApGetInfo();
	if (cfg.authmode != WIFI_AUTH_OPEN)
	{
		if (len == 0)
		{
			ESP_LOGW(TAG, "加密模式密码不能为空，忽略");
			if (auto guard = display->lockGuard())
				updateApPanel();
			return;
		}
		if (len < 8)
		{
			ESP_LOGW(TAG, "加密模式密码至少 8 位，忽略");
			if (auto guard = display->lockGuard())
				updateApPanel();
			return;
		}
	}
	wifiApSet(reinterpret_cast<const char*>(cfg.ssid), password ? password : "", cfg.authmode);
	ESP_LOGI(TAG, "AP 密码已更新");
	if (auto guard = display->lockGuard())
		updateApPanel();
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
// mDNS 主机名设置
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::doSetHostname(const char* hostname)
{
	// 校验：不为空
	if (!hostname || hostname[0] == '\0')
	{
		ESP_LOGW(TAG, "主机名为空，忽略");
		// 刷新显示保持原值
		if (auto guard = display->lockGuard())
			updateHostnameRow();
		return;
	}

	ESP_LOGI(TAG, "设置 mDNS 主机名: %s", hostname);

	if (!mdnsSetHostname(hostname))
	{
		ESP_LOGE(TAG, "mDNS 主机名设置失败");
	}

	// 刷新 UI（mdnsSetHostname 可能耗时，需在 Task 上下文中调用）
	if (auto guard = display->lockGuard())
	{
		updateHostnameRow();
	}
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
// 通用编辑弹窗（STA 密码 / AP SSID / AP 密码 / mDNS 主机名）
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::showEditDialog(EditDialogKind kind)
{
	if (m_editDialog) return;

	m_editKind = kind;

	// ── 弹窗打开时激进暂停：停定时器 + 停扫描 ──
	if (m_refreshTimer)
		lv_timer_pause(m_refreshTimer);
	if (m_scanActive)
	{
		m_scanActive = false;
		esp_wifi_scan_stop();
	}

	// ── 遮罩层（绝对像素） ──
	auto overlay = lv_obj_create(lv_layer_top());
	lv_obj_set_size(overlay, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
	lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
	lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0);
	lv_obj_set_style_border_width(overlay, 0, 0);
	lv_obj_set_style_pad_all(overlay, 0, 0);
	lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_move_foreground(overlay);

	// ── 卡片，在遮罩内居中 ──
	auto card = lv_obj_create(overlay);
	lv_obj_set_size(card, lv_pct(80), LV_SIZE_CONTENT);
	lv_obj_center(card);
	styleCard(card);
	lv_obj_set_layout(card, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(card, 12, 0);
	lv_obj_set_style_pad_top(card, 20, 0);

	m_editDialog = overlay;

	// 标题 / 副标题 / 占位符 / 密码模式
	const char* title = "";
	const char* placeholder = "";
	bool isPassword = false;
	const char* subtitle = nullptr;
	char subBuf[64];

	switch (kind)
	{
	case EditDialogKind::StaPassword:
		title = "输入 WiFi 密码";
		placeholder = "请输入密码";
		isPassword = true;
		if (m_pwdTargetIdx < m_scanResults.size())
		{
			snprintf(subBuf, sizeof(subBuf), "SSID: %s",
				reinterpret_cast<const char*>(m_scanResults[m_pwdTargetIdx].ssid));
			subtitle = subBuf;
		}
		break;
	case EditDialogKind::ApSsid:
		title = "设置 AP 名称";
		placeholder = "输入 AP SSID";
		break;
	case EditDialogKind::ApPassword:
		title = "设置 AP 密码";
		placeholder = "输入新密码（加密模式 ≥8 位）";
		isPassword = true;
		break;
	case EditDialogKind::Hostname:
		title = "设置 mDNS 主机名";
		placeholder = "输入主机名";
		subtitle = "仅允许字母、数字、连字符";
		break;
	}

	auto titleLabel = GUI::createLabel(card, title);
	lv_obj_set_style_text_font(titleLabel, FontLoader::getDefault(FontLoader::FontSize::Default), 0);

	if (subtitle)
	{
		auto sub = GUI::createLabel(card, subtitle);
		lv_obj_set_style_text_font(sub, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(sub, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_pad_bottom(sub, 8, 0);
	}

	// 输入框
	m_editTa = lv_textarea_create(card);
	lv_obj_set_width(m_editTa, lv_pct(90));
	lv_obj_set_height(m_editTa, 60);
	lv_textarea_set_one_line(m_editTa, true);
	lv_obj_set_scrollbar_mode(m_editTa, LV_SCROLLBAR_MODE_OFF);
	lv_textarea_set_password_mode(m_editTa, isPassword);
	lv_textarea_set_placeholder_text(m_editTa, placeholder);
	lv_obj_set_style_text_align(m_editTa, LV_TEXT_ALIGN_LEFT, 0);
	lv_obj_set_style_text_font(m_editTa, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	// 键盘"Enter"键 → LV_EVENT_READY → 执行确认
	lv_obj_add_event_cb(m_editTa, onEditConfirmCb, LV_EVENT_READY, this);

	// 预填当前值
	switch (kind)
	{
	case EditDialogKind::ApSsid:
	{
		wifi_ap_config_t cfg = wifiApGetInfo();
		lv_textarea_set_text(m_editTa, reinterpret_cast<const char*>(cfg.ssid));
		break;
	}
	case EditDialogKind::Hostname:
		lv_textarea_set_text(m_editTa, mdnsGetHostname());
		break;
	default:
		break;
	}

	// 键盘（固定高度，避免 flex 重算导致光标晃动）
	m_editKeyboard = lv_keyboard_create(card);
	lv_obj_set_width(m_editKeyboard, lv_pct(100));
	lv_obj_set_height(m_editKeyboard, 280);
	lv_obj_set_flex_grow(m_editKeyboard, 0);
	lv_keyboard_set_textarea(m_editKeyboard, m_editTa);
	// 键盘按钮使用 NotoSC Small（fallback 到 lv_font_get_default 含符号字型）
	lv_obj_set_style_text_font(m_editKeyboard, FontLoader::getDefault(FontLoader::FontSize::Small), LV_PART_ITEMS);
	// 键盘按键焦点高亮（参考 aiAgentOverlay）
	lv_obj_set_style_bg_color(m_editKeyboard, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_opa(m_editKeyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_text_color(m_editKeyboard, LV_COLOR_MAKE(0xff, 0xff, 0xff), LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
	lv_obj_set_style_bg_color(m_editKeyboard, LV_COLOR_MAKE(0x00, 0x77, 0xdd), LV_PART_ITEMS | LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(m_editKeyboard, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_FOCUSED);

	// 创建 Group，使摇杆可通过 lv_group_send_data 向键盘发送按键事件
	m_editGroup = lv_group_create();
	lv_group_add_obj(m_editGroup, m_editKeyboard);
	lv_group_focus_obj(m_editKeyboard);

	// 重置摇杆定时器，防弹窗打开时误触发方向导航
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;

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
	lv_obj_add_event_cb(cancelBtn, onEditCancelCb, LV_EVENT_CLICKED, this);
	// 设置按钮 label 字体（lv_layer_top 上无字体继承）
	if (auto* l = lv_obj_get_child(cancelBtn, 0))
		lv_obj_set_style_text_font(l, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	auto confirmBtn = GUI::createButton(btn_row, "确定", 120, 44);
	lv_obj_set_style_bg_color(confirmBtn, LV_COLOR_MAKE(0x00, 0x88, 0x00), 0);
	lv_obj_add_event_cb(confirmBtn, onEditConfirmCb, LV_EVENT_CLICKED, this);
	if (auto* l = lv_obj_get_child(confirmBtn, 0))
		lv_obj_set_style_text_font(l, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
}

void WifiSettingsApp::hideEditDialog()
{
	if (!m_editDialog) return;

	// LVGL 锁：可能从 BLE task（onGamepadInput）调用
	auto guard = display->lockGuard();

	if (m_editGroup) {
		lv_group_del(m_editGroup);
		m_editGroup = nullptr;
	}

	m_editKeyboard = nullptr;
	m_editTa = nullptr;
	lv_obj_del(m_editDialog);
	m_editDialog = nullptr;

	// ── 弹窗关闭时恢复刷新 ──
	if (m_refreshTimer)
		lv_timer_resume(m_refreshTimer);
}

// ════════════════════════════════════════════════════════════════
// 焦点导航
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::applyFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	auto clearFocus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj)
		{ if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clearFocus(m_backBtn);
	clearFocus(m_wifiToggleBtn);
	clearFocus(m_scanBtn);
	clearFocus(m_disconnectBtn);
	for (auto* row : m_apRows) clearFocus(row);
	for (auto* row : m_scanRows) clearFocus(row);

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		focus(m_backBtn);
		break;
	case FOCUS_AP:
		if (m_focusApIdx >= 0 && (size_t)m_focusApIdx < m_apRows.size())
		{
			focus(m_apRows[m_focusApIdx]);
			lv_obj_scroll_to_view(m_apRows[m_focusApIdx], LV_ANIM_ON);
		}
		break;
	case FOCUS_STA_TITLE:
	{
		lv_obj_t* targets[] = { m_wifiToggleBtn, m_scanBtn };
		if (m_focusStaTitleIdx >= 0 && m_focusStaTitleIdx < 2)
			focus(targets[m_focusStaTitleIdx]);
		break;
	}
	case FOCUS_STA_LIST:
		if (m_focusListIdx >= 0 && (size_t)m_focusListIdx < m_scanRows.size())
		{
			focus(m_scanRows[m_focusListIdx]);
			lv_obj_scroll_to_view(m_scanRows[m_focusListIdx], LV_ANIM_ON);
		}
		break;
	case FOCUS_STA_INFO:
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
		lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_AP:
	{
		auto guard = display->lockGuard();
		switch (m_focusApIdx)
		{
		case 0: toggleAp(); break;                                 // AP 开关
		case 1: showEditDialog(EditDialogKind::ApSsid); break;     // SSID
		case 2: showEditDialog(EditDialogKind::ApPassword); break; // 密码
		case 3: cycleAuthMode(); break;                            // 认证循环
		case 4: toggleNat(); break;                                // NAT 自动
		case 5: showEditDialog(EditDialogKind::Hostname); break;   // 主机名
		default: break;
		}
		break;
	}
	case FOCUS_STA_TITLE:
	{
		auto guard = display->lockGuard();
		lv_obj_t* targets[] = { m_wifiToggleBtn, m_scanBtn };
		if (m_focusStaTitleIdx >= 0 && m_focusStaTitleIdx < 2)
			lv_obj_send_event(targets[m_focusStaTitleIdx], LV_EVENT_CLICKED, nullptr);
		break;
	}
	case FOCUS_STA_LIST:
	{
		auto guard = display->lockGuard();
		size_t idx = (size_t)m_focusListIdx;
		if (idx < m_connectBtns.size() && m_connectBtns[idx])
		{
			lv_obj_send_event(m_connectBtns[idx], LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	case FOCUS_STA_INFO:
	{
		auto guard = display->lockGuard();
		if (m_isConnected)
			lv_obj_send_event(m_disconnectBtn, LV_EVENT_CLICKED, nullptr);
		break;
	}
	}
}

// ── AP 面板导航 ──

void WifiSettingsApp::navApUp()
{
	auto guard = display->lockGuard();
	if (m_focusApIdx > 0)
	{
		m_focusApIdx--;
	}
	else
	{
		m_focusGroup = FOCUS_TITLE;
	}
	applyFocus();
}

void WifiSettingsApp::navApDown()
{
	auto guard = display->lockGuard();
	int8_t last = (int8_t)m_apRows.size() - 1;
	if (m_focusApIdx < last)
	{
		m_focusApIdx++;
	}
	// 底部停留
	applyFocus();
}

// ── STA 头部导航（WiFi开关 ↔ 扫描） ──

void WifiSettingsApp::navStaTitleLeft()
{
	auto guard = display->lockGuard();
	if (m_focusStaTitleIdx > 0)
	{
		m_focusStaTitleIdx--;
	}
	else
	{
		// 最左 → 左出到 AP 面板
		m_focusGroup = FOCUS_AP;
	}
	applyFocus();
}

void WifiSettingsApp::navStaTitleRight()
{
	auto guard = display->lockGuard();
	if (m_focusStaTitleIdx < 1)
	{
		m_focusStaTitleIdx++;
	}
	applyFocus();
}

void WifiSettingsApp::navStaTitleUp()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_AP;
	applyFocus();
}

void WifiSettingsApp::navStaTitleDown()
{
	auto guard = display->lockGuard();
	if (!m_scanRows.empty())
	{
		m_focusGroup = FOCUS_STA_LIST;
		m_focusListIdx = 0;
	}
	else
	{
		m_focusGroup = FOCUS_STA_INFO;
	}
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
		m_focusGroup = FOCUS_STA_TITLE;
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
		m_focusGroup = FOCUS_STA_INFO;
	}
	applyFocus();
}

// ── 信息区导航（断开） ──

void WifiSettingsApp::navInfoUp()
{
	auto guard = display->lockGuard();
	if (!m_scanRows.empty())
	{
		m_focusGroup = FOCUS_STA_LIST;
		m_focusListIdx = (int8_t)m_scanRows.size() - 1;
	}
	else
	{
		m_focusGroup = FOCUS_STA_TITLE;
	}
	applyFocus();
}

void WifiSettingsApp::navInfoDown()
{
	// 已在底部（断开），不再向下
	applyFocus();
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 边沿检测 ──
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// ── 弹窗中 ──
	if (m_editDialog)
	{
		// B 键取消
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
		{
			hideEditDialog();
			if (auto guard = display->lockGuard())
				applyFocus();
			return;
		}

		// A / L3 / START → 按下当前选中的键盘按键
		if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
			(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)) ||
			(newPress & static_cast<uint16_t>(GamepadButton::BTN_START)))
		{
			if (auto guard = display->lockGuard())
			{
				if (m_editKeyboard) {
					uint16_t btn_id = lv_btnmatrix_get_selected_btn(m_editKeyboard);
					if (btn_id != LV_BUTTONMATRIX_BUTTON_NONE) {
						uint32_t id = btn_id;
						lv_obj_send_event(m_editKeyboard, LV_EVENT_VALUE_CHANGED, &id);
					}
				}
			}
			return;
		}

		// 摇杆方向 → 键盘按键导航
		{
			if (!lxLeft && !lxRight && !lyUp && !lyDown)
			{
				m_nextMoveTime[hostId][playerId] = 0;
				return;
			}

			if (m_nextMoveTime[hostId][playerId] >= xTaskGetTickCount()) return;

			TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
			m_nextMoveTime[hostId][playerId] = xTaskGetTickCount() + delay;

			uint32_t key = 0;
			if (lxLeft)  key = LV_KEY_LEFT;
			if (lxRight) key = LV_KEY_RIGHT;
			if (lyUp)    key = LV_KEY_UP;
			if (lyDown)  key = LV_KEY_DOWN;

			if (auto guard = display->lockGuard())
			{
				if (m_editGroup)
					lv_group_send_data(m_editGroup, key);
			}
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
		m_nextMoveTime[hostId][playerId] = 0;
		return;
	}
	if (m_nextMoveTime[hostId][playerId] >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[hostId][playerId] = xTaskGetTickCount() + delay;

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		// 返回（唯一项）→ 右/下进 AP 面板
		if (lxRight || lyDown)
		{
			m_focusGroup = FOCUS_AP;
			m_focusApIdx = 0;
			if (auto guard = display->lockGuard())
				applyFocus();
		}
		break;

	case FOCUS_AP:
		if (lyUp) navApUp();
		if (lyDown) navApDown();
		if (lxLeft)
		{
			m_focusGroup = FOCUS_TITLE;
			if (auto guard = display->lockGuard())
				applyFocus();
		}
		if (lxRight)
		{
			m_focusGroup = FOCUS_STA_TITLE;
			m_focusStaTitleIdx = 0;
			if (auto guard = display->lockGuard())
				applyFocus();
		}
		break;

	case FOCUS_STA_TITLE:
		if (lxLeft) navStaTitleLeft();
		if (lxRight) navStaTitleRight();
		if (lyUp) navStaTitleUp();
		if (lyDown) navStaTitleDown();
		break;

	case FOCUS_STA_LIST:
		if (lyUp) navListUp();
		if (lyDown) navListDown();
		if (lxLeft)
		{
			m_focusGroup = FOCUS_AP;
			if (auto guard = display->lockGuard())
				applyFocus();
		}
		break;

	case FOCUS_STA_INFO:
		if (lyUp) navInfoUp();
		if (lxLeft)
		{
			m_focusGroup = FOCUS_AP;
			if (auto guard = display->lockGuard())
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
	self->m_focusGroup = FOCUS_STA_TITLE;
	self->m_focusStaTitleIdx = 0;
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
	self->m_focusGroup = FOCUS_STA_TITLE;
	self->m_focusStaTitleIdx = 1;
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

	self->m_focusGroup = FOCUS_STA_LIST;
	self->m_focusListIdx = (int8_t)scanIndex;
	self->m_pwdTargetIdx = scanIndex;
	self->showEditDialog(EditDialogKind::StaPassword);
}

void WifiSettingsApp::onDisconnectBtnCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STA_INFO;
	self->doDisconnect();
}

void WifiSettingsApp::onEditConfirmCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	if (!self->m_editTa) return;

	EditDialogKind kind = self->m_editKind;
	// 复制文本（dialog 关闭后 textarea 被删除）
	char* text = strdup(lv_textarea_get_text(self->m_editTa));
	self->hideEditDialog();

	switch (kind)
	{
	case EditDialogKind::StaPassword:
		self->doConnect(self->m_pwdTargetIdx, text);
		free(text);
		break;
	case EditDialogKind::ApSsid:
		self->doSetApSsid(text);
		free(text);
		break;
	case EditDialogKind::ApPassword:
		self->doSetApPassword(text);
		free(text);
		break;
	case EditDialogKind::Hostname:
		Task::addTask([](void* param) -> TickType_t
			{
				auto* pair = static_cast<std::pair<WifiSettingsApp*, char*>*>(param);
				pair->first->doSetHostname(pair->second);
				free(pair->second);
				delete pair;
				return Task::infinityTime;
			}, "setHostname", new std::pair(self, text), 0, Task::Affinity::None);
		break;
	}
}

void WifiSettingsApp::onEditCancelCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->hideEditDialog();
	if (auto guard = self->display->lockGuard())
		self->applyFocus();
}

void WifiSettingsApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_TITLE;

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

// ── AP 面板回调 ──

void WifiSettingsApp::onApToggleCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_AP;
	self->m_focusApIdx = 0;
	Task::addTask([](void* param) -> TickType_t
		{
			auto& self = *static_cast<WifiSettingsApp*>(param);
			self.toggleAp();
			return Task::infinityTime;
		}, "apToggle", self, 0, Task::Affinity::None);
}

void WifiSettingsApp::onApSsidCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_AP;
	self->m_focusApIdx = 1;
	self->showEditDialog(EditDialogKind::ApSsid);
}

void WifiSettingsApp::onApPwdCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_AP;
	self->m_focusApIdx = 2;
	self->showEditDialog(EditDialogKind::ApPassword);
}

void WifiSettingsApp::onApAuthCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_AP;
	self->m_focusApIdx = 3;
	Task::addTask([](void* param) -> TickType_t
		{
			auto& self = *static_cast<WifiSettingsApp*>(param);
			self.cycleAuthMode();
			return Task::infinityTime;
		}, "apAuth", self, 0, Task::Affinity::None);
}

void WifiSettingsApp::onApNatCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_AP;
	self->m_focusApIdx = 4;
	Task::addTask([](void* param) -> TickType_t
		{
			auto& self = *static_cast<WifiSettingsApp*>(param);
			self.toggleNat();
			return Task::infinityTime;
		}, "apNat", self, 0, Task::Affinity::None);
}

// ════════════════════════════════════════════════════════════════
// AI 能力
// ════════════════════════════════════════════════════════════════

std::string WifiSettingsApp::getAiContext()
{
	std::string ctx = "当前在 WiFi 设置（左右分屏：左=AP 热点，右=Station）。\n"
		"可用焦点组（select 的 text 参数）：\n"
		"- title(0)\t返回\n"
		"- ap(0-5)\tAP开关/SSID/密码/认证/NAT自动/主机名\n"
		"- staTitle(0-1)\tWiFi开关/扫描\n"
		"- list(0-" + std::to_string(m_scanResults.empty() ? 0 : (int)m_scanResults.size() - 1) + ")\tAP 列表\n"
		"- info(0)\t断开\n\n";

	// AP 状态
	{
		wifi_ap_config_t cfg = wifiApGetInfo();
		ctx += "AP: ";
		ctx += wifiApIsStarted() ? "已开启" : "已关闭";
		ctx += std::string(", SSID=") + (cfg.ssid[0] ? reinterpret_cast<const char*>(cfg.ssid) : "-");
		ctx += std::string(", 认证=") + authModeName(cfg.authmode);
		if (wifiApIsStarted())
			ctx += ", 客户端=" + std::to_string(wifiApGetStaCount()) + "/4";
		ctx += std::string(", NAT自动=") + (wifiNatIsAutoStart() ? "开" : "关");
		ctx += "\n";
	}

	ctx += "扫描到 " + std::to_string(m_scanResults.size()) + " 个 AP\n";
	ctx += m_isConnected ? "WiFi: 已连接\n" : "WiFi: 未连接\n";
	ctx += std::string("mDNS 主机名: ") + mdnsGetHostname() + "\n";

	const char* groupName = "";
	switch (m_focusGroup) {
	case FOCUS_TITLE:     groupName = "title"; break;
	case FOCUS_AP:        groupName = "ap"; break;
	case FOCUS_STA_TITLE: groupName = "staTitle"; break;
	case FOCUS_STA_LIST:  groupName = "list"; break;
	case FOCUS_STA_INFO:  groupName = "info"; break;
	}
	ctx += std::string("焦点@") + groupName;
	if (m_focusGroup == FOCUS_AP) ctx += ":" + std::to_string(m_focusApIdx);
	else if (m_focusGroup == FOCUS_STA_TITLE) ctx += ":" + std::to_string(m_focusStaTitleIdx);
	else if (m_focusGroup == FOCUS_STA_LIST) ctx += ":" + std::to_string(m_focusListIdx);
	else if (m_focusGroup == FOCUS_STA_INFO) ctx += ":" + std::to_string(m_focusInfoIdx);
	ctx += "\n";
	return ctx;
}

void WifiSettingsApp::onAiSelect(const AiSelectTarget& target)
{
	auto guard = display->lockGuard();
	if (!guard) return;

	if (target.group == "title" && target.index == 0) {
		m_focusGroup = FOCUS_TITLE;
		applyFocus();
	}
	else if (target.group == "ap" && target.index >= 0 && (size_t)target.index < m_apRows.size()) {
		m_focusGroup = FOCUS_AP;
		m_focusApIdx = (int8_t)target.index;
		applyFocus();
	}
	else if (target.group == "staTitle" && target.index >= 0 && target.index <= 1) {
		m_focusGroup = FOCUS_STA_TITLE;
		m_focusStaTitleIdx = (int8_t)target.index;
		applyFocus();
	}
	else if (target.group == "list" && target.index >= 0 && (size_t)target.index < m_scanRows.size()) {
		m_focusGroup = FOCUS_STA_LIST;
		m_focusListIdx = (int8_t)target.index;
		applyFocus();
	}
	else if (target.group == "info" && target.index == 0) {
		m_focusGroup = FOCUS_STA_INFO;
		m_focusInfoIdx = 0;
		applyFocus();
	}
}

void WifiSettingsApp::onAiActivate()
{
	activateFocus();
}

// ════════════════════════════════════════════════════════════════
// mDNS 主机名回调
// ════════════════════════════════════════════════════════════════

void WifiSettingsApp::onHostnameRowCb(lv_event_t* e)
{
	auto* self = static_cast<WifiSettingsApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_AP;
	self->m_focusApIdx = 5;
	self->showEditDialog(EditDialogKind::Hostname);
}
