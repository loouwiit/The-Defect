#include "powerManagementApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "display/font.hpp"
#include "task/task.hpp"
#include "bleGamepad/bleGamepad.hpp"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_pm.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "adc_battery_estimation.h"

// ════════════════════════════════════════════════════════════════
// ADC 电池配置（根据实际硬件调整）
// ════════════════════════════════════════════════════════════════

static constexpr int ADC_UNIT = 2;           // ADC_UNIT_2
static constexpr int ADC_CHANNEL = 2;         // ADC_CHANNEL_2 (默认 ADC2_CH2)
static constexpr int ADC_ATTEN = 3;           // ADC_ATTEN_DB_12
static constexpr int ADC_BITWIDTH = 0;        // ADC_BITWIDTH_DEFAULT
static constexpr int RESISTOR_UPPER = 10.2f;    // 上拉电阻 Ω
static constexpr int RESISTOR_LOWER = 5.2f;    // 下拉电阻 Ω

// ════════════════════════════════════════════════════════════════
// 构造 / 析构
// ════════════════════════════════════════════════════════════════

PowerManagementApp::PowerManagementApp(Display* display)
	: App(display)
{
}

PowerManagementApp::~PowerManagementApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::init()
{
	App::init();

	ESP_LOGI(TAG, "address = %p 初始化", this);

	initBatteryAdc();

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

	// 立即刷新一次电量显示
	refreshBatteryUi();

	// 初始聚焦
	m_focusGroup = FOCUS_TITLE;
	applyFocus();

	ESP_LOGI(TAG, "电源管理 App 初始化完成");
}

void PowerManagementApp::deinit()
{
	// 删除定时器
	if (m_refreshTimer)
	{
		lv_timer_del(m_refreshTimer);
		m_refreshTimer = nullptr;
	}
	if (m_activityTimer)
	{
		lv_timer_del(m_activityTimer);
		m_activityTimer = nullptr;
	}
	if (m_restoreTimer)
	{
		lv_timer_del(m_restoreTimer);
		m_restoreTimer = nullptr;
	}
    if (m_adcHandle) {
        adc_battery_estimation_destroy((adc_battery_estimation_handle_t)m_adcHandle);
        m_adcHandle = nullptr;
    }

	App::deinit();
	ESP_LOGI(TAG, "电源管理 App 已释放");
}

void PowerManagementApp::onForeground()
{
	// 恢复定时器
	if (!m_refreshTimer)
	{
		m_refreshTimer = lv_timer_create(timerCb, RefreshIntervalMs, this);
	}
	else
	{
		lv_timer_resume(m_refreshTimer);
	}
	if (!m_activityTimer)
	{
		m_activityTimer = lv_timer_create(activityTimerCb, ActivityRefreshMs, this);
	}
	else
	{
		lv_timer_resume(m_activityTimer);
	}

	ESP_LOGI(TAG, "前台，定时器已启动");
}

void PowerManagementApp::onBackground()
{
	if (m_refreshTimer)
	{
		lv_timer_pause(m_refreshTimer);
	}
	if (m_activityTimer)
	{
		lv_timer_pause(m_activityTimer);
	}

	ESP_LOGI(TAG, "后台，定时器已暂停");
}

void PowerManagementApp::onGamepadConnected(uint8_t playerId)
{
	ESP_LOGI(TAG, "手柄 %d 已连接", playerId);
}

void PowerManagementApp::onGamepadDisconnected(uint8_t playerId)
{
	ESP_LOGI(TAG, "手柄 %d 已断开", playerId);
}

// ════════════════════════════════════════════════════════════════
// 定时器回调
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::timerCb(lv_timer_t* t)
{
	auto* self = static_cast<PowerManagementApp*>(lv_timer_get_user_data(t));
	self->refreshBatteryUi();
}

void PowerManagementApp::activityTimerCb(lv_timer_t* t)
{
	auto* self = static_cast<PowerManagementApp*>(lv_timer_get_user_data(t));
	self->refreshActivityIndicators();
}

// ════════════════════════════════════════════════════════════════
// 电量颜色
// ════════════════════════════════════════════════════════════════

lv_color_t PowerManagementApp::batteryColor(int percent)
{
	if (percent >= BatteryGreenMin)
		return GUI::Color::SUCCESS;        // 绿
	else if (percent >= BatteryYellowMin)
		return GUI::Color::WARNING;        // 黄
	else
		return GUI::Color::DANGER;         // 红
}

// ════════════════════════════════════════════════════════════════
// 电池读取
// ════════════════════════════════════════════════════════════════

bool PowerManagementApp::initBatteryAdc()
{
	adc_battery_estimation_t cfg = {};
    cfg.internal.adc_unit      = ADC_UNIT_2;
    cfg.internal.adc_bitwidth  = ADC_BITWIDTH_DEFAULT;
    cfg.internal.adc_atten     = ADC_ATTEN_DB_12;
    cfg.adc_channel            = ADC_CHANNEL_2;
    cfg.upper_resistor         = 10.2f;
    cfg.lower_resistor         = 5.1f;
    // battery_points / battery_points_count = nullptr/0 → 使用默认映射
    // charging_detect_cb = nullptr → 不检测充电状态

    m_adcHandle = adc_battery_estimation_create(&cfg);
    if (!m_adcHandle) {
        ESP_LOGE(TAG, "ADC 电池初始化失败");
        return false;
    }

	ESP_LOGI(TAG, "ADC 电池初始化完成（通道 %d, 分压 %d/%d）",
			 ADC_CHANNEL, RESISTOR_UPPER, RESISTOR_LOWER);
	return true;
}

int PowerManagementApp::readHostBatteryPercent()
{	
	   if (!m_adcHandle) {
        return 50;  // 未初始化时返回默认值
    }

    float capacity = 0;
    esp_err_t ret = adc_battery_estimation_get_capacity(
        (adc_battery_estimation_handle_t)m_adcHandle, &capacity);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "读取电量失败: %s", esp_err_to_name(ret));
        return m_hostBatteryPercent;  // 保持上次值
    }
    return (int)(capacity + 0.5f);

}

int PowerManagementApp::readHostVoltageMv()
{
	// 可从 adc_battery_estimation 或直接 ADC 读取电压
	int pct = m_hostBatteryPercent;
	return 3700 + (pct * 5);  // 0%→3700mV, 100%→4200mV
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::buildUi()
{
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

	m_titleLabel = GUI::createTitle(top_bar, "电源管理");
	lv_obj_set_flex_grow(m_titleLabel, 1);
	lv_obj_set_style_text_align(m_titleLabel, LV_TEXT_ALIGN_CENTER, 0);

	// 右侧等宽占位，使标题真正居中
	auto right_spacer = lv_obj_create(top_bar);
	lv_obj_set_width(right_spacer, 100);
	lv_obj_set_height(right_spacer, 44);
	lv_obj_set_style_border_width(right_spacer, 0, 0);
	lv_obj_set_style_bg_opa(right_spacer, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(right_spacer, LV_OBJ_FLAG_CLICKABLE);

	// ── 主机电量区域（flex_grow 填充中间空间） ──
	auto host_section = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(host_section, 1);
	lv_obj_set_style_pad_all(host_section, 16, 0);
	lv_obj_set_style_pad_top(host_section, 8, 0);
	lv_obj_set_style_pad_left(host_section, 32, 0);
	lv_obj_set_style_pad_right(host_section, 32, 0);
	lv_obj_set_style_border_width(host_section, 0, 0);
	lv_obj_set_style_bg_opa(host_section, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_bottom(host_section, 0, 0);

	m_hostCard = lv_obj_create(host_section);
	lv_obj_set_width(m_hostCard, lv_pct(100));
	lv_obj_set_height(m_hostCard, HostBatteryCardH);
	styleCard(m_hostCard);
	lv_obj_remove_flag(m_hostCard, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_layout(m_hostCard, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(m_hostCard, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(m_hostCard, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(m_hostCard, 16, 0);

	// 主机电池图标（大号）
	m_hostIconLabel = GUI::createLabel(m_hostCard, LV_SYMBOL_BATTERY_FULL);
	lv_obj_set_style_text_font(m_hostIconLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);

	// 百分比（大号，粗体）
	m_hostPercentLabel = GUI::createLabel(m_hostCard, "--%");
	lv_obj_set_style_text_font(m_hostPercentLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);

	// 进度条
	m_hostBar = GUI::createProgressBar(m_hostCard, lv_pct(80), 0, 100, 0);
	lv_obj_set_height(m_hostBar, 20);
	lv_obj_set_style_radius(m_hostBar, 10, 0);
	lv_obj_set_style_bg_color(m_hostBar, LV_COLOR_MAKE(0x40, 0x40, 0x40), LV_PART_MAIN);
	lv_obj_set_style_bg_color(m_hostBar, GUI::Color::SUCCESS, LV_PART_INDICATOR);
	lv_obj_set_style_radius(m_hostBar, 10, LV_PART_INDICATOR);

	// 电压信息
	m_hostVoltageLabel = GUI::createLabel(m_hostCard, "---- mV");
	lv_obj_set_style_text_font(m_hostVoltageLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_style_text_color(m_hostVoltageLabel, GUI::Color::SUBTLE, 0);

	// ── 手柄电量区域（固定在底部） ──
	auto slot_section = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(slot_section, 8, 0);
	lv_obj_set_style_pad_left(slot_section, 16, 0);
	lv_obj_set_style_pad_right(slot_section, 16, 0);
	lv_obj_set_style_border_width(slot_section, 0, 0);
	lv_obj_set_style_bg_opa(slot_section, LV_OPA_TRANSP, 0);

	// 标题
	auto slot_title_row = GUI::createFlex(slot_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(slot_title_row, 0, 0);
	lv_obj_set_style_border_width(slot_title_row, 0, 0);
	lv_obj_set_style_bg_opa(slot_title_row, LV_OPA_TRANSP, 0);
	lv_obj_remove_flag(slot_title_row, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_remove_flag(slot_title_row, LV_OBJ_FLAG_CLICKABLE);

	auto slot_title = GUI::createSubtitle(slot_title_row, "手柄电量");
	lv_obj_set_style_text_font(slot_title, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_flex_grow(slot_title, 1);

	// 手柄槽位 — 一行 4 个
	auto slots_row = GUI::createFlex(slot_section, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_column(slots_row, 8, 0);
	lv_obj_set_style_pad_all(slots_row, 0, 0);
	lv_obj_set_style_pad_top(slots_row, 4, 0);
	lv_obj_set_style_border_width(slots_row, 0, 0);
	lv_obj_set_style_bg_opa(slots_row, LV_OPA_TRANSP, 0);

	for (int i = 0; i < MaxPlayers; i++)
	{
		auto card = lv_obj_create(slots_row);
		lv_obj_set_flex_grow(card, 1);
		lv_obj_set_height(card, SlotCardH);
		styleCard(card);
		lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(card, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_layout(card, LV_LAYOUT_FLEX);
		lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
		lv_obj_set_style_pad_all(card, 4, 0);
		lv_obj_set_style_pad_left(card, 6, 0);
		lv_obj_set_style_pad_right(card, 6, 0);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_user_data(card, (void*)(uintptr_t)i);
		lv_obj_add_event_cb(card, [](lv_event_t* e) {
			auto* self = static_cast<PowerManagementApp*>(lv_event_get_user_data(e));
			auto* c = static_cast<lv_obj_t*>(lv_event_get_target(e));
			self->m_focusGroup = FOCUS_SLOTS;
			self->m_focusSlotsIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(c);
			self->applyFocus();
			}, LV_EVENT_CLICKED, this);
		m_slotCards[i] = card;

		// 名称
		char slotText[16];
		snprintf(slotText, sizeof(slotText), "P%d", i + 1);
		auto label = GUI::createLabel(card, slotText);
		lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_width(label, lv_pct(100));
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
		lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
		m_slotLabels[i] = label;

		// 进度条
		auto bar = GUI::createProgressBar(card, lv_pct(80), 0, 100, 0);
		lv_obj_set_height(bar, 10);
		lv_obj_set_style_radius(bar, 5, 0);
		lv_obj_set_style_bg_color(bar, LV_COLOR_MAKE(0x40, 0x40, 0x40), LV_PART_MAIN);
		lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
		m_slotBars[i] = bar;

		// 百分比
		auto pctLabel = GUI::createLabel(card, "--%");
		lv_obj_set_style_text_font(pctLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		m_slotPercentLabels[i] = pctLabel;

		m_slotConnected[i] = false;
	}

	// ── 操作按钮行 ──
	auto btn_section = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(btn_section, 12, 0);
	lv_obj_set_style_pad_left(btn_section, 32, 0);
	lv_obj_set_style_pad_right(btn_section, 32, 0);
	lv_obj_set_style_pad_bottom(btn_section, 24, 0);
	lv_obj_set_style_border_width(btn_section, 0, 0);
	lv_obj_set_style_bg_opa(btn_section, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_column(btn_section, 16, 0);

	// 关机按钮
	m_shutdownBtn = GUI::createButton(btn_section, "", lv_pct(50), 52);
	lv_obj_set_style_bg_color(m_shutdownBtn, LV_COLOR_MAKE(0x60, 0x20, 0x20), 0);
	lv_obj_set_style_radius(m_shutdownBtn, 12, 0);
	lv_obj_set_style_border_width(m_shutdownBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_shutdownBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_shutdownBtn, onShutdownBtnCb, LV_EVENT_CLICKED, this);
	m_shutdownBtnLabel = lv_obj_get_child(m_shutdownBtn, 0);
	lv_label_set_text(m_shutdownBtnLabel, LV_SYMBOL_POWER " 关机");

	// 低功耗模式按钮
	m_lowPowerBtn = GUI::createButton(btn_section, "", lv_pct(50), 52);
	lv_obj_set_style_bg_color(m_lowPowerBtn, LV_COLOR_MAKE(0x30, 0x40, 0x60), 0);
	lv_obj_set_style_radius(m_lowPowerBtn, 12, 0);
	lv_obj_set_style_border_width(m_lowPowerBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_lowPowerBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_lowPowerBtn, onLowPowerBtnCb, LV_EVENT_CLICKED, this);
	m_lowPowerBtnLabel = lv_obj_get_child(m_lowPowerBtn, 0);
	lv_label_set_text(m_lowPowerBtnLabel, LV_SYMBOL_CHARGE " 低功耗");
}

// ════════════════════════════════════════════════════════════════
// 刷新主机电量
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::refreshBatteryUi()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	// 读取主机电量
	m_hostBatteryPercent = readHostBatteryPercent();
	m_hostVoltageMv = readHostVoltageMv();

	// 更新 UI
	lv_color_t color = batteryColor(m_hostBatteryPercent);

	// 电量图标（根据百分比选择对应图标）
	const char* icon;
	if (m_hostBatteryPercent >= 80)
		icon = LV_SYMBOL_BATTERY_FULL;
	else if (m_hostBatteryPercent >= 60)
		icon = LV_SYMBOL_BATTERY_3;
	else if (m_hostBatteryPercent >= 40)
		icon = LV_SYMBOL_BATTERY_2;
	else if (m_hostBatteryPercent >= 20)
		icon = LV_SYMBOL_BATTERY_1;
	else
		icon = LV_SYMBOL_BATTERY_EMPTY;

	lv_label_set_text(m_hostIconLabel, icon);
	lv_obj_set_style_text_color(m_hostIconLabel, color, 0);

	// 百分比
	char pctStr[8];
	snprintf(pctStr, sizeof(pctStr), "%d%%", m_hostBatteryPercent);
	lv_label_set_text(m_hostPercentLabel, pctStr);
	lv_obj_set_style_text_color(m_hostPercentLabel, color, 0);

	// 进度条
	lv_bar_set_value(m_hostBar, m_hostBatteryPercent, LV_ANIM_ON);
	lv_obj_set_style_bg_color(m_hostBar, color, LV_PART_INDICATOR);

	// 电压
	char voltStr[16];
	snprintf(voltStr, sizeof(voltStr), "%d mV", m_hostVoltageMv);
	lv_label_set_text(m_hostVoltageLabel, voltStr);

	// 刷新手柄电量
	refreshSlotUi();
}

// ════════════════════════════════════════════════════════════════
// 刷新手柄电量
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::refreshSlotUi()
{
	for (int i = 0; i < MaxPlayers; i++)
	{
		auto* ctx = BleGamepad::instance().getDevice(i);
		bool isConnected = (ctx && ctx->connected);

		if (isConnected)
		{
			uint8_t battery = ctx->batteryLevel;
			int percent = (battery <= 100) ? battery : 0;

			// 名称
			char slotText[40];
			snprintf(slotText, sizeof(slotText), "P%d: %s", i + 1, ctx->name);
			lv_label_set_text(m_slotLabels[i], slotText);

			// 百分比
			char pctStr[8];
			snprintf(pctStr, sizeof(pctStr), "%d%%", percent);
			lv_label_set_text(m_slotPercentLabels[i], pctStr);

			// 进度条
			lv_bar_set_value(m_slotBars[i], percent, LV_ANIM_ON);
			lv_color_t color = batteryColor(percent);
			lv_obj_set_style_bg_color(m_slotBars[i], color, LV_PART_INDICATOR);
			lv_obj_set_style_text_color(m_slotPercentLabels[i], color, 0);

			m_slotConnected[i] = true;
		}
		else
		{
			lv_label_set_text(m_slotLabels[i], "P" LV_SYMBOL_CLOSE);
			lv_label_set_text(m_slotPercentLabels[i], "--%");

			lv_bar_set_value(m_slotBars[i], 0, LV_ANIM_OFF);
			lv_obj_set_style_bg_color(m_slotBars[i], GUI::Color::SUBTLE, LV_PART_INDICATOR);
			lv_obj_set_style_text_color(m_slotPercentLabels[i], GUI::Color::SUBTLE, 0);

			m_slotConnected[i] = false;
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 活动指示刷新
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::refreshActivityIndicators()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	static const lv_color_t activeBg = LV_COLOR_MAKE(0x45, 0x45, 0x60);

	TickType_t now = xTaskGetTickCount();
	for (int i = 0; i < MaxPlayers; i++)
	{
		if (!m_slotConnected[i]) continue;

		bool active = (now - m_lastActivityTime[i] < ActivityTimeout);
		if (active != m_lastActivityStatus[i])
		{
			m_lastActivityStatus[i] = active;
			lv_obj_set_style_bg_color(m_slotCards[i],
				active ? activeBg : GUI::Color::CARD, 0);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 操作
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::doShutdown()
{
	ESP_LOGW(TAG, "执行关机（Deep-sleep）");

	// 使用 LVGL 异步删除所有对象后再进入睡眠
	// 实际使用时应确保所有外设已停止
	BleGamepad::instance().stopScan();

	// 配置唤醒源：默认定时器 60 秒后唤醒（便于调试）
	esp_sleep_enable_timer_wakeup(60 * 1000000ULL);
	// 也可使用外部 GPIO 唤醒（根据实际硬件配置）
	// esp_sleep_enable_ext1_wakeup_io(BIT_NUM(GPIO_NUM_XX), ESP_EXT1_WAKEUP_ANY_HIGH);

	esp_deep_sleep_start();
}

void PowerManagementApp::doToggleLowPower()
{
	mLowPowerActive = !mLowPowerActive;

	if (mLowPowerActive)
	{
		ESP_LOGI(TAG, "启用低功耗模式（Light-sleep）");

		// 配置电源管理策略
		esp_pm_config_t pm_config = {
			.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
			.min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
			.light_sleep_enable = true,
		};
		esp_err_t ret = esp_pm_configure(&pm_config);
		if (ret != ESP_OK)
		{
			ESP_LOGE(TAG, "低功耗模式配置失败: %s", esp_err_to_name(ret));
			mLowPowerActive = false;

			auto guard = display->lockGuard();
			if (guard)
			{
				lv_label_set_text(m_lowPowerBtnLabel, LV_SYMBOL_CHARGE " 低功耗");
			}
			return;
		}

		auto guard = display->lockGuard();
		if (guard)
		{
			// 由于 LVGL 无唤醒符号，复用充电符号
			lv_label_set_text(m_lowPowerBtnLabel, LV_SYMBOL_CHARGE " 正常模式");
			lv_obj_set_style_bg_color(m_lowPowerBtn, LV_COLOR_MAKE(0x20, 0x50, 0x30), 0);
		}
	}
	else
	{
		ESP_LOGI(TAG, "关闭低功耗模式");

		// 关闭 light-sleep
		esp_pm_config_t pm_config = {
			.max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
			.min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
			.light_sleep_enable = false,
		};
		esp_pm_configure(&pm_config);

		auto guard = display->lockGuard();
		if (guard)
		{
			lv_label_set_text(m_lowPowerBtnLabel, LV_SYMBOL_CHARGE " 低功耗");
			lv_obj_set_style_bg_color(m_lowPowerBtn, LV_COLOR_MAKE(0x30, 0x40, 0x60), 0);
		}
	}
}

// ════════════════════════════════════════════════════════════════
// 焦点导航
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::applyFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	auto clearFocus = [](lv_obj_t* obj)
	{
		if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED);
	};
	auto focus = [](lv_obj_t* obj)
	{
		if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED);
	};

	clearFocus(m_backBtn);
	clearFocus(m_shutdownBtn);
	clearFocus(m_lowPowerBtn);
	for (auto* card : m_slotCards) clearFocus(card);

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		focus(m_backBtn);
		break;
	case FOCUS_SLOTS:
		if (m_focusSlotsIdx >= 0 && m_focusSlotsIdx < MaxPlayers)
			focus(m_slotCards[m_focusSlotsIdx]);
		break;
	case FOCUS_BUTTONS:
		focus(m_focusBtnIdx == 0 ? m_shutdownBtn : m_lowPowerBtn);
		break;
	}
}

void PowerManagementApp::activateFocus()
{
	auto guard = display->lockGuard();
	if (!guard) return;

	switch (m_focusGroup)
	{
	case FOCUS_TITLE:
		lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr);
		break;
	case FOCUS_SLOTS:
		// 槽位仅显示信息，点击无操作
		break;
	case FOCUS_BUTTONS:
		if (m_focusBtnIdx == 0)
			lv_obj_send_event(m_shutdownBtn, LV_EVENT_CLICKED, nullptr);
		else
			lv_obj_send_event(m_lowPowerBtn, LV_EVENT_CLICKED, nullptr);
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// 更新活动时间
	const bool hasActivity = state.buttons != 0 || lxLeft || lxRight || lyUp || lyDown;
	if (hasActivity)
		m_lastActivityTime[playerId] = xTaskGetTickCount();

	// ── B 键：返回 / 退出 ──
	if (state.isPressed(GamepadButton::BTN_B))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			popApp();
		}
	}

	// ── A 键：激活 ──
	if (state.isPressed(GamepadButton::BTN_A) || state.isPressed(GamepadButton::BTN_L3))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			activateFocus();
		}
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
		if (lyDown)
		{
			m_focusGroup = FOCUS_SLOTS;
			m_focusSlotsIdx = 0;
			applyFocus();
		}
		break;

	case FOCUS_SLOTS:
		if (lxLeft && m_focusSlotsIdx > 0)
		{
			m_focusSlotsIdx--;
			applyFocus();
		}
		if (lxRight && m_focusSlotsIdx < MaxPlayers - 1)
		{
			m_focusSlotsIdx++;
			applyFocus();
		}
		if (lyUp)
		{
			m_focusGroup = FOCUS_TITLE;
			applyFocus();
		}
		if (lyDown)
		{
			m_focusGroup = FOCUS_BUTTONS;
			m_focusBtnIdx = 0;
			applyFocus();
		}
		break;

	case FOCUS_BUTTONS:
		if (lxLeft && m_focusBtnIdx > 0)
		{
			m_focusBtnIdx = 0;
			applyFocus();
		}
		if (lxRight && m_focusBtnIdx < 1)
		{
			m_focusBtnIdx = 1;
			applyFocus();
		}
		if (lyUp)
		{
			m_focusGroup = FOCUS_SLOTS;
			m_focusSlotsIdx = MaxPlayers - 1;
			applyFocus();
		}
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void PowerManagementApp::onBackBtnCb(lv_event_t* e)
{
	auto* self = static_cast<PowerManagementApp*>(lv_event_get_user_data(e));

	self->m_focusGroup = FOCUS_TITLE;

	Task::addTask([](void* param) -> TickType_t
		{
			auto* app = static_cast<PowerManagementApp*>(param);
			if (app->getManager())
			{
				app->popApp();
			}
			return Task::infinityTime;
		}, "powerMgrBack", self, 0, Task::Affinity::None);
}

void PowerManagementApp::onShutdownBtnCb(lv_event_t* e)
{
	auto* self = static_cast<PowerManagementApp*>(lv_event_get_user_data(e));
	auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
	auto* label = lv_obj_get_child(btn, 0);

	// 视觉反馈
	lv_label_set_text(label, "关机中...");
	lv_obj_set_style_bg_color(btn, LV_COLOR_MAKE(0x80, 0x10, 0x10), 0);

	// 由于 Deep-sleep 会立即生效，延后执行以使 LVGL 有机会刷新
	Task::addTask([](void* param) -> TickType_t
		{
			auto* self = static_cast<PowerManagementApp*>(param);
			self->doShutdown();
			return Task::infinityTime;
		}, "powerMgrShutdown", self, 500, Task::Affinity::None);
}

void PowerManagementApp::onLowPowerBtnCb(lv_event_t* e)
{
	auto* self = static_cast<PowerManagementApp*>(lv_event_get_user_data(e));

	Task::addTask([](void* param) -> TickType_t
		{
			auto* self = static_cast<PowerManagementApp*>(param);
			self->doToggleLowPower();
			return Task::infinityTime;
		}, "powerMgrLowPower", self, 0, Task::Affinity::None);
}
