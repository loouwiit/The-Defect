#include "timeSettingsApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "wifi/wifi.hpp"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <ctime>
#include <cstring>
#include <cstdlib>

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

TimeSettingsApp::TimeSettingsApp(Display* display)
	: App(display)
{
}

TimeSettingsApp::~TimeSettingsApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::init()
{
	App::init();

	ESP_LOGI(TAG, "address = %p", this);

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
	m_focusAdjustIdx = 0;
	m_focusSyncIdx = 0;
	applyFocus();

	updateTimeDisplay();

	m_refreshTimer = lv_timer_create(timerCb, RefreshInterval, this);

	ESP_LOGI(TAG, "时间设置 App 初始化完成");
}

void TimeSettingsApp::deinit()
{
	if (m_refreshTimer)
	{
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}

	App::deinit();
	ESP_LOGI(TAG, "时间设置 App 已释放");
}

void TimeSettingsApp::onForeground()
{
	if (m_refreshTimer)
		lv_timer_resume(m_refreshTimer);
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + 500;
	ESP_LOGI(TAG, "前台");
}

void TimeSettingsApp::onBackground()
{
	if (m_refreshTimer)
		lv_timer_pause(m_refreshTimer);
	ESP_LOGI(TAG, "后台");
}

// ════════════════════════════════════════════════════════════════
// 定时器
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::timerCb(lv_timer_t* t)
{
	static_cast<TimeSettingsApp*>(lv_timer_get_user_data(t))->refreshUi();
}

void TimeSettingsApp::refreshUi()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	updateTimeDisplay();

	// SNTP 同步状态
	if (m_syncInProgress)
	{
		time_t now = time(nullptr);
		struct tm timeinfo;
		localtime_r(&now, &timeinfo);

		if (timeinfo.tm_year > (2024 - 1900))
		{
			m_syncInProgress = false;
			lv_label_set_text(m_syncStatusLabel, "已同步");
			lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::SUCCESS, 0);
			lv_obj_clear_state(m_syncNowBtn, LV_STATE_DISABLED);
			lv_obj_set_style_bg_color(m_syncNowBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
			ESP_LOGI(TAG, "SNTP 同步成功");
		}
		else if (xTaskGetTickCount() - m_syncStartTick >= pdMS_TO_TICKS(SyncTimeout))
		{
			m_syncInProgress = false;
			lv_label_set_text(m_syncStatusLabel, "同步超时");
			lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::WARNING, 0);
			lv_obj_clear_state(m_syncNowBtn, LV_STATE_DISABLED);
			lv_obj_set_style_bg_color(m_syncNowBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
			ESP_LOGW(TAG, "SNTP 同步超时");
		}
	}

	// WiFi 状态
	bool wifiOk = wifiIsConnect();
	const char* cur = lv_label_get_text(m_wifiStatusLabel);
	if (wifiOk && strcmp(cur, "WiFi: 已连接") != 0)
	{
		lv_label_set_text(m_wifiStatusLabel, "WiFi: 已连接");
		lv_obj_set_style_text_color(m_wifiStatusLabel, GUI::Color::SUCCESS, 0);
		if (m_autoSync && !m_syncInProgress)
		{
			time_t now = time(nullptr);
			struct tm timeinfo;
			localtime_r(&now, &timeinfo);
			if (timeinfo.tm_year <= (2024 - 1900))
				doSyncNow();
		}
	}
	else if (!wifiOk && strcmp(cur, "WiFi: 未连接") != 0)
	{
		lv_label_set_text(m_wifiStatusLabel, "WiFi: 未连接");
		lv_obj_set_style_text_color(m_wifiStatusLabel, GUI::Color::SUBTLE, 0);
	}
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::buildUi()
{
	lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 顶栏（右侧加与返回按钮等宽占位，使标题视觉居中） ──
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

	auto title = GUI::createTitle(top_bar, "时间设置");
	lv_obj_set_flex_grow(title, 1);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

	// 右侧占位，宽度与返回按钮一致，使标题居中
	auto right_placeholder = lv_obj_create(top_bar);
	lv_obj_set_size(right_placeholder, 100, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(right_placeholder, 0, 0);
	lv_obj_set_style_bg_opa(right_placeholder, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(right_placeholder, LV_OBJ_FLAG_CLICKABLE);

	// ── 状态行（右对齐，独立于顶栏和内容区） ──
	auto status_row = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(status_row, 0, 0);
	lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(status_row, 0, 0);
	lv_obj_set_style_pad_right(status_row, 16, 0);
	lv_obj_set_flex_align(status_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	auto status_box = GUI::createFlex(status_row, LV_FLEX_FLOW_COLUMN, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(status_box, 0, 0);
	lv_obj_set_style_bg_opa(status_box, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(status_box, 0, 0);
	lv_obj_clear_flag(status_box, LV_OBJ_FLAG_CLICKABLE);

	m_wifiStatusLabel = GUI::createLabel(status_box, "");
	lv_obj_set_style_text_font(m_wifiStatusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_align(m_wifiStatusLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_width(m_wifiStatusLabel, LV_SIZE_CONTENT);

	m_syncStatusLabel = GUI::createLabel(status_box, "");
	lv_obj_set_style_pad_top(m_syncStatusLabel, 2, 0);
	lv_obj_set_style_text_font(m_syncStatusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_align(m_syncStatusLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_width(m_syncStatusLabel, LV_SIZE_CONTENT);

	// ── 内容区（flex_grow 填满剩余高度，禁止滚动） ──
	auto content = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), lv_pct(100));
	lv_obj_set_style_pad_all(content, 4, 0);
	lv_obj_set_style_pad_left(content, 28, 0);
	lv_obj_set_style_pad_right(content, 28, 0);
	lv_obj_set_style_border_width(content, 0, 0);
	lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
	lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

	// ═══════════════════════════
	// 时间显示
	// ═══════════════════════════
	auto time_area = GUI::createFlex(content, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(time_area, 8, 0);
	lv_obj_set_style_border_width(time_area, 0, 0);
	lv_obj_set_style_bg_opa(time_area, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(time_area, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	m_timeLabel = GUI::createLabel(time_area, "--:--:--");
	lv_obj_set_style_text_font(m_timeLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_set_style_text_color(m_timeLabel, GUI::Color::TEXT, 0);

	m_dateLabel = GUI::createLabel(time_area, "");
	lv_obj_set_style_text_font(m_dateLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_dateLabel, GUI::Color::SUBTLE, 0);

	// ═══════════════════════════
	// 手动调整
	// ═══════════════════════════
	auto adjust_section = GUI::createFlex(content, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(adjust_section, 4, 0);
	lv_obj_set_style_border_width(adjust_section, 0, 0);
	lv_obj_set_style_bg_opa(adjust_section, LV_OPA_TRANSP, 0);

	auto adjust_title = GUI::createSubtitle(adjust_section, "手动调整");
	lv_obj_set_style_text_font(adjust_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	// 小时
	auto hour_row = GUI::createFlex(adjust_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(hour_row, 2, 0);
	lv_obj_set_style_border_width(hour_row, 0, 0);
	lv_obj_set_style_bg_opa(hour_row, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(hour_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	auto hl = GUI::createLabel(hour_row, "时");
	lv_obj_set_style_text_font(hl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(hl, GUI::Color::SUBTLE, 0);
	lv_obj_set_width(hl, 28);

	m_hourDownBtn = GUI::createButton(hour_row, "−", 48, 38);
	lv_obj_set_style_radius(m_hourDownBtn, 6, 0);
	lv_obj_set_style_bg_color(m_hourDownBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_hourDownBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_hourDownBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_hourDownBtn, onHourDownCb, LV_EVENT_CLICKED, this);

	m_hourValueLabel = GUI::createLabel(hour_row, "00");
	lv_obj_set_style_text_font(m_hourValueLabel, FontLoader::getDefault(FontLoader::FontSize::Medium), 0);
	lv_obj_set_style_text_color(m_hourValueLabel, GUI::Color::TEXT, 0);
	lv_obj_set_width(m_hourValueLabel, 56);
	lv_obj_set_style_text_align(m_hourValueLabel, LV_TEXT_ALIGN_CENTER, 0);

	m_hourUpBtn = GUI::createButton(hour_row, "+", 48, 38);
	lv_obj_set_style_radius(m_hourUpBtn, 6, 0);
	lv_obj_set_style_bg_color(m_hourUpBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_hourUpBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_hourUpBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_hourUpBtn, onHourUpCb, LV_EVENT_CLICKED, this);

	// 分钟
	auto min_row = GUI::createFlex(adjust_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(min_row, 2, 0);
	lv_obj_set_style_border_width(min_row, 0, 0);
	lv_obj_set_style_bg_opa(min_row, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(min_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	auto ml = GUI::createLabel(min_row, "分");
	lv_obj_set_style_text_font(ml, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(ml, GUI::Color::SUBTLE, 0);
	lv_obj_set_width(ml, 28);

	m_minDownBtn = GUI::createButton(min_row, "−", 48, 38);
	lv_obj_set_style_radius(m_minDownBtn, 6, 0);
	lv_obj_set_style_bg_color(m_minDownBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_minDownBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_minDownBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_minDownBtn, onMinDownCb, LV_EVENT_CLICKED, this);

	m_minValueLabel = GUI::createLabel(min_row, "00");
	lv_obj_set_style_text_font(m_minValueLabel, FontLoader::getDefault(FontLoader::FontSize::Medium), 0);
	lv_obj_set_style_text_color(m_minValueLabel, GUI::Color::TEXT, 0);
	lv_obj_set_width(m_minValueLabel, 56);
	lv_obj_set_style_text_align(m_minValueLabel, LV_TEXT_ALIGN_CENTER, 0);

	m_minUpBtn = GUI::createButton(min_row, "+", 48, 38);
	lv_obj_set_style_radius(m_minUpBtn, 6, 0);
	lv_obj_set_style_bg_color(m_minUpBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_minUpBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_minUpBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_minUpBtn, onMinUpCb, LV_EVENT_CLICKED, this);

	// 秒 + 归零
	auto sec_row = GUI::createFlex(adjust_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(sec_row, 2, 0);
	lv_obj_set_style_border_width(sec_row, 0, 0);
	lv_obj_set_style_bg_opa(sec_row, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(sec_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	auto sl = GUI::createLabel(sec_row, "秒");
	lv_obj_set_style_text_font(sl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(sl, GUI::Color::SUBTLE, 0);
	lv_obj_set_width(sl, 28);

	m_secValueLabel = GUI::createLabel(sec_row, "00");
	lv_obj_set_style_text_font(m_secValueLabel, FontLoader::getDefault(FontLoader::FontSize::Medium), 0);
	lv_obj_set_style_text_color(m_secValueLabel, GUI::Color::TEXT, 0);
	lv_obj_set_width(m_secValueLabel, 56);
	lv_obj_set_style_text_align(m_secValueLabel, LV_TEXT_ALIGN_CENTER, 0);

	m_secResetBtn = GUI::createButton(sec_row, "归零", 70, 38);
	lv_obj_set_style_radius(m_secResetBtn, 6, 0);
	lv_obj_set_style_bg_color(m_secResetBtn, LV_COLOR_MAKE(0x30, 0x30, 0x40), 0);
	lv_obj_set_style_border_width(m_secResetBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_secResetBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_secResetBtn, onSecResetCb, LV_EVENT_CLICKED, this);

	// ═══════════════════════════
	// 网络同步
	// ═══════════════════════════
	auto sync_section = GUI::createFlex(content, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(sync_section, 6, 0);
	lv_obj_set_style_pad_bottom(sync_section, 4, 0);
	lv_obj_set_style_border_width(sync_section, 0, 0);
	lv_obj_set_style_bg_opa(sync_section, LV_OPA_TRANSP, 0);

	auto sync_title = GUI::createSubtitle(sync_section, "网络时间同步");
	lv_obj_set_style_text_font(sync_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	// 自动同步
	auto auto_row = GUI::createFlex(sync_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(auto_row, 4, 0);
	lv_obj_set_style_border_width(auto_row, 0, 0);
	lv_obj_set_style_bg_opa(auto_row, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(auto_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	m_autoSyncLabel = GUI::createLabel(auto_row, "自动同步");
	lv_obj_set_style_text_font(m_autoSyncLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_autoSyncLabel, GUI::Color::TEXT, 0);

	m_autoSyncToggle = GUI::createButton(auto_row, "OFF", 72, 32);
	m_autoSyncToggleLabel = lv_obj_get_child(m_autoSyncToggle, 0);
	lv_obj_set_style_radius(m_autoSyncToggle, 16, 0);
	lv_obj_set_style_bg_color(m_autoSyncToggle, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
	lv_obj_set_style_border_width(m_autoSyncToggle, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_autoSyncToggle, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_autoSyncToggle, onAutoSyncCb, LV_EVENT_CLICKED, this);

	// 立即同步
	auto sync_now_row = GUI::createFlex(sync_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_top(sync_now_row, 4, 0);
	lv_obj_set_style_border_width(sync_now_row, 0, 0);
	lv_obj_set_style_bg_opa(sync_now_row, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(sync_now_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	m_syncNowBtn = GUI::createButton(sync_now_row, "立即同步", 140, 38);
	lv_obj_set_style_radius(m_syncNowBtn, 6, 0);
	lv_obj_set_style_bg_color(m_syncNowBtn, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
	lv_obj_set_style_border_width(m_syncNowBtn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_syncNowBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_syncNowBtn, onSyncNowCb, LV_EVENT_CLICKED, this);
}

// ════════════════════════════════════════════════════════════════
// 时间显示
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::updateTimeDisplay()
{
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_r(&now, &timeinfo);

	if (timeinfo.tm_year > (2024 - 1900))
	{
		char buf[16];
		strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
		lv_label_set_text(m_timeLabel, buf);

		char dateBuf[32];
		strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %a", &timeinfo);
		lv_label_set_text(m_dateLabel, dateBuf);

		char vb[4];
		snprintf(vb, sizeof(vb), "%02d", timeinfo.tm_hour);
		lv_label_set_text(m_hourValueLabel, vb);
		snprintf(vb, sizeof(vb), "%02d", timeinfo.tm_min);
		lv_label_set_text(m_minValueLabel, vb);
		snprintf(vb, sizeof(vb), "%02d", timeinfo.tm_sec);
		lv_label_set_text(m_secValueLabel, vb);

		if (!m_syncInProgress)
		{
			const char* c = lv_label_get_text(m_syncStatusLabel);
			if (strcmp(c, "") == 0 || strcmp(c, "等待 WiFi 连接...") == 0)
			{
				lv_label_set_text(m_syncStatusLabel, "已同步");
				lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::SUCCESS, 0);
			}
		}
	}
	else
	{
		lv_label_set_text(m_timeLabel, "--:--:--");
		lv_label_set_text(m_dateLabel, "等待时间同步...");
		char vb[4];
		snprintf(vb, sizeof(vb), "%02d", timeinfo.tm_hour);
		lv_label_set_text(m_hourValueLabel, vb);
		snprintf(vb, sizeof(vb), "%02d", timeinfo.tm_min);
		lv_label_set_text(m_minValueLabel, vb);
		snprintf(vb, sizeof(vb), "%02d", timeinfo.tm_sec);
		lv_label_set_text(m_secValueLabel, vb);
	}
}

// ════════════════════════════════════════════════════════════════
// 手动调整
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::adjustHour(int delta)
{
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_r(&now, &timeinfo);
	timeinfo.tm_hour += delta;
	time_t nt = mktime(&timeinfo);
	struct timeval tv = { nt, 0 };
	settimeofday(&tv, nullptr);
	updateTimeDisplay();
	ESP_LOGI(TAG, "调整小时: %+d", delta);
}

void TimeSettingsApp::adjustMinute(int delta)
{
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_r(&now, &timeinfo);
	timeinfo.tm_min += delta;
	time_t nt = mktime(&timeinfo);
	struct timeval tv = { nt, 0 };
	settimeofday(&tv, nullptr);
	updateTimeDisplay();
	ESP_LOGI(TAG, "调整分钟: %+d", delta);
}

void TimeSettingsApp::resetSeconds()
{
	time_t now = time(nullptr);
	struct tm timeinfo;
	localtime_r(&now, &timeinfo);
	timeinfo.tm_sec = 0;
	time_t nt = mktime(&timeinfo);
	struct timeval tv = { nt, 0 };
	settimeofday(&tv, nullptr);
	updateTimeDisplay();
	ESP_LOGI(TAG, "秒归零");
}

// ════════════════════════════════════════════════════════════════
// SNTP 同步
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::toggleAutoSync()
{
	m_autoSync = !m_autoSync;
	if (m_autoSync)
	{
		lv_label_set_text(m_autoSyncToggleLabel, "ON");
		lv_obj_set_style_bg_color(m_autoSyncToggle, LV_COLOR_MAKE(0x30, 0x50, 0x30), 0);
		if (wifiIsConnect())
		{
			time_t now = time(nullptr);
			struct tm timeinfo;
			localtime_r(&now, &timeinfo);
			if (timeinfo.tm_year <= (2024 - 1900))
				doSyncNow();
			else {
				lv_label_set_text(m_syncStatusLabel, "已同步");
				lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::SUCCESS, 0);
			}
		}
		else
		{
			lv_label_set_text(m_syncStatusLabel, "等待 WiFi 连接...");
			lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::SUBTLE, 0);
		}
	}
	else
	{
		lv_label_set_text(m_autoSyncToggleLabel, "OFF");
		lv_obj_set_style_bg_color(m_autoSyncToggle, LV_COLOR_MAKE(0x50, 0x30, 0x30), 0);
		m_syncInProgress = false;
		lv_label_set_text(m_syncStatusLabel, "手动模式");
		lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::SUBTLE, 0);
	}
	ESP_LOGI(TAG, "自动同步: %s", m_autoSync ? "ON" : "OFF");
}

void TimeSettingsApp::doSyncNow()
{
	if (m_syncInProgress) return;
	if (!wifiIsConnect())
	{
		lv_label_set_text(m_syncStatusLabel, "请先连接 WiFi");
		lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::WARNING, 0);
		return;
	}

	m_syncInProgress = true;
	m_syncStartTick = xTaskGetTickCount();
	lv_label_set_text(m_syncStatusLabel, "同步中...");
	lv_obj_set_style_text_color(m_syncStatusLabel, GUI::Color::WARNING, 0);
	lv_obj_add_state(m_syncNowBtn, LV_STATE_DISABLED);
	lv_obj_set_style_bg_color(m_syncNowBtn, LV_COLOR_MAKE(0x50, 0x50, 0x50), 0);

	// 初始化 SNTP（乐鑫官方推荐方式）
	esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
	config.start = true;
	config.server_from_dhcp = false;
	config.renew_servers_after_new_IP = true;
	config.index_of_first_server = 0;
	config.ip_event_to_renew = IP_EVENT_STA_GOT_IP;
	esp_netif_sntp_init(&config);

	// 设置时区（东八区）
	setenv("TZ", "CST-8", 1);
	tzset();

	ESP_LOGI(TAG, "SNTP 同步启动");
}

// ════════════════════════════════════════════════════════════════
// 焦点系统
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::applyFocus()
{
	auto clr = [](lv_obj_t* o) { if (o) lv_obj_clear_state(o, LV_STATE_FOCUSED); };
	auto foc = [](lv_obj_t* o) { if (o) lv_obj_add_state(o, LV_STATE_FOCUSED); };

	clr(m_backBtn);
	clr(m_hourDownBtn); clr(m_hourUpBtn);
	clr(m_minDownBtn); clr(m_minUpBtn);
	clr(m_secResetBtn);
	clr(m_autoSyncToggle);
	clr(m_syncNowBtn);

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		foc(m_backBtn);
		break;

	case FOCUS_ADJUST:
	{
		lv_obj_t* tgt[] = { m_hourDownBtn, m_hourUpBtn, m_minDownBtn, m_minUpBtn, m_secResetBtn };
		if (m_focusAdjustIdx >= 0 && m_focusAdjustIdx < 5) foc(tgt[m_focusAdjustIdx]);
		break;
	}

	case FOCUS_SYNC:
	{
		lv_obj_t* tgt[] = { m_autoSyncToggle, m_syncNowBtn };
		if (m_focusSyncIdx >= 0 && m_focusSyncIdx < 2) foc(tgt[m_focusSyncIdx]);
		break;
	}
	}
}

void TimeSettingsApp::activateFocus()
{
	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		Task::addTask([](void* p) -> TickType_t
			{
				static_cast<TimeSettingsApp*>(p)->popApp();
				return Task::infinityTime;
			}, "popTime", this, 0, Task::Affinity::NotAssigned);
		break;

	case FOCUS_ADJUST:
	{
		lv_obj_t* tgt[] = { m_hourDownBtn, m_hourUpBtn, m_minDownBtn, m_minUpBtn, m_secResetBtn };
		if (m_focusAdjustIdx >= 0 && m_focusAdjustIdx < 5)
		{
			auto g = display->lockGuard();
			lv_obj_send_event(tgt[m_focusAdjustIdx], LV_EVENT_CLICKED, nullptr);
		}
		break;
	}

	case FOCUS_SYNC:
	{
		lv_obj_t* tgt[] = { m_autoSyncToggle, m_syncNowBtn };
		if (m_focusSyncIdx >= 0 && m_focusSyncIdx < 2)
		{
			auto g = display->lockGuard();
			lv_obj_send_event(tgt[m_focusSyncIdx], LV_EVENT_CLICKED, nullptr);
		}
		break;
	}
	}
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t dz = 50, ct = 128;

	bool lxL = state.lx < ct - dz;
	bool lxR = state.lx > ct + dz;
	bool lyU = state.ly < ct - dz;
	bool lyD = state.ly > ct + dz;

	uint16_t np = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	if (np & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			Task::addTask([](void* p) -> TickType_t
				{
					static_cast<TimeSettingsApp*>(p)->popApp();
					return Task::infinityTime;
				}, "popTime", this, 0, Task::Affinity::NotAssigned);
		}
		return;
	}

	if ((np & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(np & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			activateFocus();
		}
	}

	if (!lxL && !lxR && !lyU && !lyD) { m_nextMoveTime[playerId] = 0; return; }
	if (m_nextMoveTime[playerId] >= xTaskGetTickCount()) return;

	TickType_t d = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + d;

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		if (lxR) { m_focusGroup = FOCUS_ADJUST; m_focusAdjustIdx = 0; applyFocus(); }
		if (lyD) { m_focusGroup = FOCUS_ADJUST; m_focusAdjustIdx = 0; applyFocus(); }
		break;

	case FOCUS_ADJUST:
		if (lxL && m_focusAdjustIdx > 0) { m_focusAdjustIdx--; applyFocus(); }
		if (lxR && m_focusAdjustIdx < 4) { m_focusAdjustIdx++; applyFocus(); }
		if (lyU) { m_focusGroup = FOCUS_TITLE; applyFocus(); }
		if (lyD) { m_focusGroup = FOCUS_SYNC; m_focusSyncIdx = 0; applyFocus(); }
		break;

	case FOCUS_SYNC:
		if (lxL && m_focusSyncIdx > 0) { m_focusSyncIdx--; applyFocus(); }
		if (lxR && m_focusSyncIdx < 1) { m_focusSyncIdx++; applyFocus(); }
		if (lyU) { m_focusGroup = FOCUS_ADJUST; m_focusAdjustIdx = 0; applyFocus(); }
		if (lyD) { m_focusGroup = FOCUS_TITLE; applyFocus(); }
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 回调
// ════════════════════════════════════════════════════════════════

void TimeSettingsApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<TimeSettingsApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤，请等待%ums", self->m_nextActionTime - xTaskGetTickCount());
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + 500;

	Task::addTask([](void* p) -> TickType_t
		{
			static_cast<TimeSettingsApp*>(p)->popApp();
			return Task::infinityTime;
		}, "popTime", self, 0, Task::Affinity::NotAssigned);
}

void TimeSettingsApp::onHourDownCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->adjustHour(-1);
}

void TimeSettingsApp::onHourUpCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->adjustHour(1);
}

void TimeSettingsApp::onMinDownCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->adjustMinute(-1);
}

void TimeSettingsApp::onMinUpCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->adjustMinute(1);
}

void TimeSettingsApp::onSecResetCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->resetSeconds();
}

void TimeSettingsApp::onAutoSyncCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->toggleAutoSync();
}

void TimeSettingsApp::onSyncNowCb(lv_event_t* e)
{
	static_cast<TimeSettingsApp*>(lv_event_get_user_data(e))->doSyncNow();
}
