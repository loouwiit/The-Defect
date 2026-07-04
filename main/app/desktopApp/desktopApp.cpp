#include "desktopApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "app/testApp/testApp.hpp"
#include "app/bleSettingsApp/bleSettingsApp.hpp"
#include "app/wifiSettingsApp/wifiSettingsApp.hpp"
#include "app/timeSettingsApp/timeSettingsApp.hpp"
#include "app/powerManagementApp/powerManagementApp.hpp"
#include "app/snake/snakeRoom/snakeRoom.hpp"
#include "app/tetris/tetrisApp.hpp"
#include "audio/Audio.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/font.hpp"
#include "display/ili9881c.hpp"
#include "adc_battery_estimation.h"
#include <ctime>

// ========== 游戏数据 ==========

const char* DesktopApp::GAME_NAMES[] =
{
	"贪吃蛇",
	"水果忍者",
	"俄罗斯方块",
	"斗地主",
	"中国象棋",
};

const char* DesktopApp::GAME_DESCS[] =
{
	"经典贪吃蛇游戏，吃食物成长！",
	"快速切水果，享受爽快连击！",
	"经典俄罗斯方块，消除行数得分！",
	"三人斗地主，比拼牌技！",
	"中国象棋，楚河汉界对弈！",
};

const char* DesktopApp::GAME_ICONS[] =
{
	"F:system/desktop/snake.jpg",
	"F:system/desktop/fruitNinja.jpg",
	"F:system/desktop/tetris.jpg",
	"F:system/desktop/douDiZhu.jpg",
	"F:system/desktop/chess.jpg",
};

DesktopApp::DesktopApp(Display* display)
	: App(display)
{
}

DesktopApp::~DesktopApp() = default;

// ════════════════════════════════════════════════════════════════
// 生命周期
// ════════════════════════════════════════════════════════════════

void DesktopApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}

	// 页面背景
	lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	buildUi();

	// 创建时间更新定时器（每 10 秒更新一次）
	m_timeTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		time_t now = time(nullptr);
		struct tm timeinfo;
		localtime_r(&now, &timeinfo);
		if (timeinfo.tm_year > (2024 - 1900))
		{
			char buf[8];
			strftime(buf, sizeof(buf), "%H:%M", &timeinfo);
			lv_label_set_text(self->m_timeLabel, buf);
		}
		}, 10000, this);
	lv_timer_ready(m_timeTimer); // 首帧立即触发

	// 初始聚焦
	m_focusGroup = FOCUS_CARDS;
	m_focusCardsIdx = 0;
	applyFocus();

	// ── 初始化电池 ADC ──
	adc_battery_estimation_t batCfg = {};
	batCfg.internal.adc_unit      = ADC_UNIT_2;
	batCfg.internal.adc_bitwidth  = ADC_BITWIDTH_DEFAULT;
	batCfg.internal.adc_atten     = ADC_ATTEN_DB_12;
	batCfg.adc_channel            = ADC_CHANNEL_2;
	batCfg.upper_resistor         = 10.2f;
	batCfg.lower_resistor         = 5.1f;
	m_batteryHandle = adc_battery_estimation_create(&batCfg);

	m_batteryTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		self->updateBatteryIcon();
	}, 5000, this);
	updateBatteryIcon();

	ESP_LOGI(TAG, "桌面初始化完成");
}

void DesktopApp::deinit()
{
	if (m_timeTimer)
	{
		lv_timer_del(m_timeTimer);
		m_timeTimer = nullptr;
	}
	if (m_batteryTimer) {
		lv_timer_del(m_batteryTimer);
		m_batteryTimer = nullptr;
	}
	if (m_batteryHandle) {
		adc_battery_estimation_destroy((adc_battery_estimation_handle_t)m_batteryHandle);
		m_batteryHandle = nullptr;
	}
	App::deinit();
	ESP_LOGI(TAG, "桌面已释放");
}

void DesktopApp::onForeground()
{
	// 防抖重置
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	for (auto& i : m_nextMoveTime) i = 0;
	m_prevButtons = 0xFFFF;

	ESP_LOGI(TAG, "前台");
}

void DesktopApp::onBackground()
{
	ESP_LOGI(TAG, "后台");
}

// ════════════════════════════════════════════════════════════════
// UI 构建
// ════════════════════════════════════════════════════════════════

void DesktopApp::buildUi()
{
	// ── 主区域（flex 列布局，撑满屏幕） ──
	auto main_area = lv_obj_create(screen);
	lv_obj_set_size(main_area, lv_pct(100), lv_pct(100));
	lv_obj_set_layout(main_area, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(main_area, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_border_width(main_area, 0, 0);
	lv_obj_set_style_bg_opa(main_area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_pad_all(main_area, 0, 0);

	// ── 顶部状态行 ──
	auto status_row = GUI::createFlex(main_area, LV_FLEX_FLOW_ROW,
		lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(status_row, 8, 0);
	lv_obj_set_style_pad_left(status_row, 16, 0);
	lv_obj_set_style_pad_right(status_row, 16, 0);
	lv_obj_set_style_border_width(status_row, 0, 0);
	lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);

	m_timeLabel = GUI::createLabel(status_row, "--:--");
	lv_obj_set_style_text_color(m_timeLabel, GUI::Color::TEXT, 0);
	lv_obj_add_flag(m_timeLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_timeLabel, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = 0;
		Task::addTask([](void* param) -> TickType_t
			{
				auto* app = static_cast<DesktopApp*>(param);
				if (app->getManager()) {
					auto* timeApp = new TimeSettingsApp(app->getDisplay());
					app->getManager()->pushToNewStack(timeApp);
				}
				return Task::infinityTime;
			}, "openTimeSettings", self, 0, Task::Affinity::NotAssigned);
		}, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_timeLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_timeLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_timeLabel, 2, LV_STATE_FOCUSED);

	// ── 弹性分隔符（flex_grow 参考系，分隔时钟与状态图标） ──
	m_statusSpacer = lv_obj_create(status_row);
	lv_obj_set_size(m_statusSpacer, 0, 0);
	lv_obj_set_flex_grow(m_statusSpacer, SpaceGrow);
	lv_obj_set_style_border_width(m_statusSpacer, 0, 0);
	lv_obj_set_style_bg_opa(m_statusSpacer, LV_OPA_TRANSP, 0);
	lv_obj_clear_flag(m_statusSpacer, LV_OBJ_FLAG_CLICKABLE);

	m_wifiLabel = GUI::createLabel(status_row, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_color(m_wifiLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_wifiLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_wifiLabel, 16, 0);
	lv_obj_add_flag(m_wifiLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_wifiLabel, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = 1;
		/* LVGL 事件回调中持锁，栈操作须延后 */
		Task::addTask([](void* param) -> TickType_t
			{
				auto* app = static_cast<DesktopApp*>(param);
				if (app->getManager()) {
					auto* wifiApp = new WifiSettingsApp(app->getDisplay());
					app->getManager()->pushToNewStack(wifiApp);
				}
				return Task::infinityTime;
			}, "openWifiSettings", self, 0, Task::Affinity::NotAssigned);
		}, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_wifiLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_wifiLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_wifiLabel, 2, LV_STATE_FOCUSED);

	m_bluetoothLabel = GUI::createLabel(status_row, LV_SYMBOL_BLUETOOTH);
	lv_obj_set_style_text_color(m_bluetoothLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_bluetoothLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_bluetoothLabel, 16, 0);
	lv_obj_add_flag(m_bluetoothLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_bluetoothLabel, onBluetoothLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_bluetoothLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_bluetoothLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_bluetoothLabel, 2, LV_STATE_FOCUSED);

	// 音量
	m_volumeLabel = GUI::createLabel(status_row, LV_SYMBOL_VOLUME_MAX);
	lv_obj_set_style_text_color(m_volumeLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_volumeLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_volumeLabel, 16, 0);
	lv_obj_add_flag(m_volumeLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_volumeLabel, onVolumeLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_volumeLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_volumeLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_volumeLabel, 2, LV_STATE_FOCUSED);

	// 亮度
	m_brightnessLabel = GUI::createLabel(status_row, LV_SYMBOL_EYE_OPEN);
	lv_obj_set_style_text_color(m_brightnessLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_brightnessLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_brightnessLabel, 16, 0);
	lv_obj_add_flag(m_brightnessLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_brightnessLabel, onBrightnessLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_brightnessLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_brightnessLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_brightnessLabel, 2, LV_STATE_FOCUSED);

	m_batteryLabel = GUI::createLabel(status_row, LV_SYMBOL_BATTERY_FULL);
	lv_obj_set_style_text_font(m_batteryLabel, LV_FONT_DEFAULT, 0);
	lv_obj_add_flag(m_batteryLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_batteryLabel, onBatteryLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_outline_width(m_batteryLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_batteryLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_batteryLabel, 2, LV_STATE_FOCUSED);

	// ── 页面标题（居中） ──
	auto title = GUI::createTitle(main_area, "游戏中心");
	lv_obj_set_width(title, lv_pct(100));
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_top(title, 20, 0);
	lv_obj_set_style_pad_bottom(title, 10, 0);

	// ── 卡片区域（flex_grow 填充中间） ──
	auto cards_section = GUI::createFlex(main_area, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(cards_section, 1);
	lv_obj_set_style_pad_all(cards_section, 10, 0);
	lv_obj_set_style_pad_bottom(cards_section, 140, 0);
	lv_obj_set_style_pad_column(cards_section, 8, 0);
	lv_obj_set_style_border_width(cards_section, 0, 0);
	lv_obj_set_style_bg_opa(cards_section, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(cards_section, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 左箭头（触控备选，不参与焦点导航）
	lv_obj_set_style_pad_left(cards_section, 32, 0);
	m_prevBtn = GUI::createButton(cards_section, "<", 52, 52);
	lv_obj_set_style_radius(m_prevBtn, 30, 0);
	lv_obj_add_event_cb(m_prevBtn, onPrevBtnCb, LV_EVENT_CLICKED, this);

	// 卡片行（可触摸横向滚动）
	m_cardsRow = GUI::createFlex(cards_section, LV_FLEX_FLOW_ROW, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(m_cardsRow, 1);
	lv_obj_set_style_pad_column(m_cardsRow, 16, 0);
	lv_obj_set_style_border_width(m_cardsRow, 0, 0);
	lv_obj_set_style_bg_opa(m_cardsRow, LV_OPA_TRANSP, 0);
	lv_obj_set_scrollbar_mode(m_cardsRow, LV_SCROLLBAR_MODE_OFF);
	lv_obj_set_flex_align(m_cardsRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 创建五个游戏卡片
	for (int i = 0; i < GAME_COUNT; i++)
	{
		auto card = lv_obj_create(m_cardsRow);
		lv_obj_set_size(card, CARD_W, CARD_H);
		lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(card, 16, 0);
		lv_obj_set_style_border_width(card, 0, 0);
		lv_obj_set_style_pad_all(card, 0, 0);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_user_data(card, (void*)(uintptr_t)i);

		// 聚焦样式 — 仅用彩色边框，无阴影
		lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(card, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_border_side(card, LV_BORDER_SIDE_FULL, LV_STATE_FOCUSED);

		// 图片
		auto img = lv_image_create(card);
		lv_image_set_src(img, GAME_ICONS[i]);
		lv_obj_set_size(img, lv_pct(100), lv_pct(100));
		lv_obj_center(img);

		// 点击聚焦
		lv_obj_add_event_cb(card, [](lv_event_t* e) {
			auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
			auto* c = static_cast<lv_obj_t*>(lv_event_get_target(e));
			self->m_focusGroup = FOCUS_CARDS;
			self->m_focusCardsIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(c);
			self->updateSelectionLabels();
			self->applyFocus();
			}, LV_EVENT_CLICKED, this);

		m_gameCards[i] = card;
	}

	// 右箭头（触控备选，不参与焦点导航）
	lv_obj_set_style_pad_right(cards_section, 32, 0);
	m_nextBtn = GUI::createButton(cards_section, ">", 52, 52);
	lv_obj_set_style_radius(m_nextBtn, 30, 0);
	lv_obj_add_event_cb(m_nextBtn, onNextBtnCb, LV_EVENT_CLICKED, this);

	// ── 底部区域 ──
	// 左侧：游戏简介（弹性宽度，自动换行）
	m_descLabel = GUI::createLabel(screen, "");
	lv_obj_set_style_text_color(m_descLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_descLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_width(m_descLabel, lv_pct(40));
	lv_label_set_long_mode(m_descLabel, LV_LABEL_LONG_WRAP);
	lv_obj_align(m_descLabel, LV_ALIGN_BOTTOM_LEFT, 40, -80);

	// 右侧：开始按钮（固定右下角）
	m_startBtn = GUI::createButton(screen, "开始游戏", 160, 50);
	lv_obj_set_style_radius(m_startBtn, 12, 0);
	lv_obj_set_style_border_width(m_startBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_startBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_startBtn, onStartBtnCb, LV_EVENT_CLICKED, this);
	lv_obj_align(m_startBtn, LV_ALIGN_BOTTOM_RIGHT, -50, -50);

	// 已选择提示（在开始按钮上方，随文字长度动态调整水平位置）
	m_infoLabel = GUI::createLabel(screen, "");
	lv_obj_set_style_text_color(m_infoLabel, GUI::Color::TEXT, 0);
	lv_obj_set_width(m_infoLabel, 300);
	lv_obj_set_style_text_align(m_infoLabel, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_align_to(m_infoLabel, m_startBtn, LV_ALIGN_OUT_TOP_RIGHT, 0, -8);

	// 初始化卡片状态
	updateSelectionLabels();
	applyFocus();
}

// ════════════════════════════════════════════════════════════════
// 选中标签更新
// ════════════════════════════════════════════════════════════════

void DesktopApp::updateSelectionLabels()
{
	if (m_descLabel)
	{
		lv_label_set_text(m_descLabel, GAME_DESCS[m_focusCardsIdx]);
	}
	if (m_infoLabel)
	{
		lv_label_set_text_fmt(m_infoLabel, "已选择: %s", GAME_NAMES[m_focusCardsIdx]);
	}
}

// ════════════════════════════════════════════════════════════════
// 焦点系统
// ════════════════════════════════════════════════════════════════

void DesktopApp::applyFocus()
{
	auto guard = display->lockGuard();
	auto clearFocus = [](lv_obj_t* obj)
		{
			if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED);
		};

	clearFocus(m_timeLabel);
	clearFocus(m_wifiLabel);
	clearFocus(m_bluetoothLabel);
	clearFocus(m_volumeLabel);
	clearFocus(m_brightnessLabel);
	clearFocus(m_batteryLabel);
	for (auto* card : m_gameCards) clearFocus(card);
	clearFocus(m_startBtn);

	// 设置当前聚焦对象的 LV_STATE_FOCUSED
	auto focus = [](lv_obj_t* obj)
		{
			if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED);
		};

	switch (m_focusGroup)
	{
	case FOCUS_CARDS:
		if (m_focusCardsIdx >= 0 && m_focusCardsIdx < GAME_COUNT)
		{
			focus(m_gameCards[m_focusCardsIdx]);
			// 刷新布局后滚动到视图
			lv_obj_scroll_to_view(m_gameCards[m_focusCardsIdx], LV_ANIM_OFF);
		}
		break;

	case FOCUS_BOTTOM:
		focus(m_startBtn);
		break;

	case FOCUS_STATUS:
	{
		lv_obj_t* targets[] = { m_timeLabel, m_wifiLabel, m_bluetoothLabel, m_volumeLabel, m_brightnessLabel, m_batteryLabel };
		if (m_focusStatusIdx >= 0 && m_focusStatusIdx < 6)
			focus(targets[m_focusStatusIdx]);
		break;
	}
	}
}

void DesktopApp::activateFocus()
{
	switch (m_focusGroup)
	{
	case FOCUS_CARDS:
		startGame();
		break;

	case FOCUS_BOTTOM:
		startGame();
		break;

	case FOCUS_STATUS:
	{
		lv_obj_t* target[]{ m_timeLabel, m_wifiLabel, m_bluetoothLabel, m_volumeLabel, m_brightnessLabel, m_batteryLabel };
		auto guard = display->lockGuard();
		lv_obj_send_event(target[m_focusStatusIdx],
			LV_EVENT_CLICKED, nullptr);
		break;
	}
	}
}

// ── 卡片导航 ──

void DesktopApp::navCardsLeft()
{
	if (m_focusCardsIdx <= 0)
		return;
	auto guard = display->lockGuard();
	m_focusCardsIdx--;
	updateSelectionLabels();
	applyFocus();
	ESP_LOGI(TAG, "已选择: %s (index=%d)", GAME_NAMES[m_focusCardsIdx], m_focusCardsIdx);
}

void DesktopApp::navCardsRight()
{
	if (m_focusCardsIdx >= GAME_COUNT - 1)
		return;
	auto guard = display->lockGuard();
	m_focusCardsIdx++;
	updateSelectionLabels();
	applyFocus();
	ESP_LOGI(TAG, "已选择: %s (index=%d)", GAME_NAMES[m_focusCardsIdx], m_focusCardsIdx);
}

// ── 底部导航 ──

void DesktopApp::navToBottom()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_BOTTOM;
	applyFocus();
}

void DesktopApp::navFromBottomUp()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_CARDS;
	applyFocus();
}

// ── 状态栏导航 ──

void DesktopApp::navToStatus()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_STATUS;
	m_focusStatusIdx = 0;
	applyFocus();
}

void DesktopApp::navFromStatusDown()
{
	auto guard = display->lockGuard();
	m_focusGroup = FOCUS_CARDS;
	applyFocus();
}

// ── 操作 ──

void DesktopApp::startGame()
{
	ESP_LOGI(TAG, "启动游戏: %s", GAME_NAMES[m_focusCardsIdx]);

	if (!m_manager)
	{
		ESP_LOGE(TAG, "startGame: no manager set");
		return;
	}

	switch (m_focusCardsIdx)
	{
	case 0:
		// 贪吃蛇 — 通过 AppStack 启动
		m_manager->pushToNewStack(new SnakeRoom(display));
		break;

	case 1:
		m_manager->pushToNewStack(new TestApp(display, esp_random()));
		break;

	case 2:
		// 俄罗斯方块 — 通过 AppStack 启动
		m_manager->pushToNewStack(new TetrisApp(display));
		break;

	default:
		// 其他游戏 — TestApp 占位
		m_manager->pushToNewStack(new TestApp(display, esp_random()));
		break;
	}
}

// ════════════════════════════════════════════════════════════════
// 手柄输入
// ════════════════════════════════════════════════════════════════

void DesktopApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 边沿检测：仅刚按下的按钮有效 ──
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// ── BTN_B: 滑块激活时重置并收起 ──
	// if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	// {
	// }

	// ── 激活 (BTN_A / BTN_L3) ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
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
	case FOCUS_CARDS:
		if (lxLeft)  navCardsLeft();
		if (lxRight) navCardsRight();
		if (lyUp)    navToStatus();
		if (lyDown)  navToBottom();
		break;

	case FOCUS_BOTTOM:
		if (lyUp) navFromBottomUp();
		break;

	case FOCUS_STATUS:
		// 音量滑块激活时的特殊处理
		if (m_volumeSliderActive && m_focusStatusIdx == 3)
		{
			if (lxLeft) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_volumeSlider);
				v = v < 5 ? 0 : v - 5;
				lv_slider_set_value(m_volumeSlider, v, LV_ANIM_OFF);
				Audio::setMasterVolume(v);
				m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
			}
			if (lxRight) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_volumeSlider);
				v = v > 95 ? 100 : v + 5;
				lv_slider_set_value(m_volumeSlider, v, LV_ANIM_OFF);
				Audio::setMasterVolume(v);
				m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
			}
		}

		// 亮度滑块激活时的特殊处理
		if (m_brightnessSliderActive && m_focusStatusIdx == 4)
		{
			if (lxLeft) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_brightnessSlider);
				v = v < 5 ? 0 : v - 5;
				lv_slider_set_value(m_brightnessSlider, v, LV_ANIM_OFF);
				display->setBrightness(v);
				m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
			}
			if (lxRight) {
				auto guard = display->lockGuard();
				int v = lv_slider_get_value(m_brightnessSlider);
				v = v > 95 ? 100 : v + 5;
				lv_slider_set_value(m_brightnessSlider, v, LV_ANIM_OFF);
				display->setBrightness(v);
				m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
			}
		}

		// 非滑块激活时的状态栏导航
		if (!(m_volumeSliderActive && m_focusStatusIdx == 3) &&
			!(m_brightnessSliderActive && m_focusStatusIdx == 4))
		{
			if (lxLeft && m_focusStatusIdx > 0)
			{
				m_focusStatusIdx--;
				applyFocus();
			}
			if (lxRight && m_focusStatusIdx < 5)
			{
				m_focusStatusIdx++;
				applyFocus();
			}
		}

		// 下判定，离开状态栏导航
		if (lyDown)
		{
			// 如果音量或亮度滑块激活，先收起再导航
			if (m_volumeSliderActive)
				lv_async_call([](void* param) { static_cast<DesktopApp*>(param)->hideVolumeSlider(); }, this);
			if (m_brightnessSliderActive)
				lv_async_call([](void* param) { static_cast<DesktopApp*>(param)->hideBrightnessSlider(); }, this);
			navFromStatusDown();
		}
	}
}

// ════════════════════════════════════════════════════════════════
// LVGL 事件回调
// ════════════════════════════════════════════════════════════════

void DesktopApp::onPrevBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_CARDS;
	self->navCardsLeft();
}

void DesktopApp::onNextBtnCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_CARDS;
	self->navCardsRight();
}

void DesktopApp::onStartBtnCb(lv_event_t* e)
{
	auto* app = static_cast<DesktopApp*>(lv_event_get_user_data(e));

	/* 防抖 */
	if (app->m_nextActionTime >= xTaskGetTickCount())
		return;
	app->m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;

	/* LVGL 事件回调中持有 LVGL 锁，栈操作必须延后到 Task 中执行 */
	Task::addTask([](void* param) -> TickType_t
		{
			auto* self = static_cast<DesktopApp*>(param);
			self->m_focusGroup = FOCUS_BOTTOM;
			self->startGame();
			return Task::infinityTime;
		}, "deferredStart", app, 0, Task::Affinity::None);
}

void DesktopApp::onBluetoothLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));

	self->m_focusGroup = FOCUS_STATUS;

	/* LVGL 事件回调中持锁，栈操作须延后 */
	Task::addTask([](void* param) -> TickType_t
		{
			auto* app = static_cast<DesktopApp*>(param);
			if (app->getManager()) {
				auto* bleApp = new BleSettingsApp(app->getDisplay());
				app->getManager()->pushToNewStack(bleApp);
			}
			return Task::infinityTime;
		}, "openBleSettings", self, 0, Task::Affinity::None);
}

// ════════════════════════════════════════════════════════════════
// 亮度滑块
// ════════════════════════════════════════════════════════════════

void DesktopApp::showBrightnessSlider()
{
	if (m_brightnessSliderActive) return;

	int cur = display->getBrightness();
	m_brightnessOnOpen = cur;

	m_brightnessSlider = lv_slider_create(lv_obj_get_parent(m_brightnessLabel));
	lv_slider_set_range(m_brightnessSlider, 0, 100);
	lv_slider_set_value(m_brightnessSlider, cur, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(m_brightnessSlider, lv_color_hex(0x555555), LV_PART_MAIN);
	lv_obj_set_style_bg_color(m_brightnessSlider, lv_color_white(), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(m_brightnessSlider, lv_color_white(), LV_PART_KNOB);
	lv_obj_set_height(m_brightnessSlider, 28);
	lv_obj_set_flex_grow(m_brightnessSlider, 1);  // 初始 1，立即参与 grow 分配
	lv_obj_set_style_pad_left(m_brightnessSlider, 8, 0);
	lv_obj_set_style_pad_right(m_brightnessSlider, 16, 0);
	lv_obj_move_to_index(m_brightnessSlider, lv_obj_get_index(m_brightnessLabel) + 1);  // 动态定位到亮度标签之后
	lv_obj_add_event_cb(m_brightnessSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		int val = lv_slider_get_value(self->m_brightnessSlider);
		self->display->setBrightness(val);
		self->m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_VALUE_CHANGED, this);
	lv_obj_add_event_cb(m_brightnessSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_RELEASED, this);

	m_brightnessSliderActive = true;
	m_brightnessSliderTimeout = xTaskGetTickCount() + 3000;

	// 创建超时定时器（200ms 周期检查）
	m_brightnessSliderTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		if (!self->m_brightnessSliderActive) {
			lv_timer_del(t);
			return;
		}
		if (xTaskGetTickCount() > self->m_brightnessSliderTimeout) {
			self->hideBrightnessSlider();
		}
		}, 200, this);
	lv_timer_set_repeat_count(m_brightnessSliderTimer, -1);  // 无限重复

	// 动画展开（flex_grow: 1 → 60）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_brightnessSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, 1, SliderGrow);
	lv_anim_set_time(&a, SliderOpenTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_start(&a);

	ESP_LOGI(TAG, "亮度滑块展开: %d%%", cur);
}

void DesktopApp::hideBrightnessSlider()
{
	if (!m_brightnessSliderActive || !m_brightnessSlider) return;
	m_brightnessSliderActive = false;

	auto guard = display->lockGuard();

	// 删除超时定时器
	if (m_brightnessSliderTimer) {
		lv_timer_del(m_brightnessSliderTimer);
		m_brightnessSliderTimer = nullptr;
	}

	// 反向动画收起（flex_grow: 60 → 1）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_brightnessSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, SliderGrow, 1);
	lv_anim_set_time(&a, SliderCloseTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_set_deleted_cb(&a, [](lv_anim_t* anim) {
		if (anim->var)
			lv_obj_delete_async(static_cast<lv_obj_t*>(anim->var));
		});
	lv_anim_start(&a);

	// 值变化时才保存
	if (lv_slider_get_value(m_brightnessSlider) != m_brightnessOnOpen)
		display->saveBrightness();
	m_brightnessSlider = nullptr;
	ESP_LOGI(TAG, "亮度滑块收起");
}

void DesktopApp::onBrightnessSliderCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	int val = lv_slider_get_value(self->m_brightnessSlider);
	self->display->setBrightness(val);
}

// ════════════════════════════════════════════════════════════════
// 音量滑块
// ════════════════════════════════════════════════════════════════

void DesktopApp::showVolumeSlider()
{
	if (m_volumeSliderActive) return;

	int cur = Audio::getMasterVolume();
	m_volumeOnOpen = cur;

	m_volumeSlider = lv_slider_create(lv_obj_get_parent(m_volumeLabel));
	lv_slider_set_range(m_volumeSlider, 0, 100);
	lv_slider_set_value(m_volumeSlider, cur, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(m_volumeSlider, lv_color_hex(0x555555), LV_PART_MAIN);
	lv_obj_set_style_bg_color(m_volumeSlider, lv_color_white(), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(m_volumeSlider, lv_color_white(), LV_PART_KNOB);
	lv_obj_set_height(m_volumeSlider, 28);
	lv_obj_set_flex_grow(m_volumeSlider, 1);  // 初始 1，立即参与 grow 分配
	lv_obj_set_style_pad_left(m_volumeSlider, 8, 0);
	lv_obj_set_style_pad_right(m_volumeSlider, 16, 0);
	lv_obj_move_to_index(m_volumeSlider, lv_obj_get_index(m_volumeLabel) + 1);  // 动态定位到音量标签之后
	lv_obj_add_event_cb(m_volumeSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		int val = lv_slider_get_value(self->m_volumeSlider);
		Audio::setMasterVolume(val);
		self->m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_VALUE_CHANGED, this);
	lv_obj_add_event_cb(m_volumeSlider, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_volumeSliderTimeout = xTaskGetTickCount() + 3000;
		}, LV_EVENT_RELEASED, this);

	m_volumeSliderActive = true;
	m_volumeSliderTimeout = xTaskGetTickCount() + 3000;

	// 创建超时定时器（200ms 周期检查）
	m_volumeSliderTimer = lv_timer_create([](lv_timer_t* t) {
		auto* self = static_cast<DesktopApp*>(lv_timer_get_user_data(t));
		if (!self->m_volumeSliderActive) {
			lv_timer_del(t);
			return;
		}
		if (xTaskGetTickCount() > self->m_volumeSliderTimeout) {
			self->hideVolumeSlider();
		}
		}, 200, this);
	lv_timer_set_repeat_count(m_volumeSliderTimer, -1);  // 无限重复

	// 动画展开（flex_grow: 1 → 60）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_volumeSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, 1, SliderGrow);
	lv_anim_set_time(&a, SliderOpenTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_start(&a);

	ESP_LOGI(TAG, "音量滑块展开: %d%%", cur);
}

void DesktopApp::hideVolumeSlider()
{
	if (!m_volumeSliderActive || !m_volumeSlider) return;
	m_volumeSliderActive = false;

	auto guard = display->lockGuard();

	// 删除超时定时器
	if (m_volumeSliderTimer) {
		lv_timer_del(m_volumeSliderTimer);
		m_volumeSliderTimer = nullptr;
	}

	// 反向动画收起（flex_grow: 60 → 1）
	lv_anim_t a;
	lv_anim_init(&a);
	lv_anim_set_var(&a, m_volumeSlider);
	lv_anim_set_exec_cb(&a, [](void* var, int32_t v) {
		lv_obj_set_flex_grow(static_cast<lv_obj_t*>(var), (uint8_t)v);
		});
	lv_anim_set_values(&a, SliderGrow, 1);
	lv_anim_set_time(&a, SliderCloseTime);
	lv_anim_set_path_cb(&a, lv_anim_path_linear);
	lv_anim_set_deleted_cb(&a, [](lv_anim_t* anim) {
		if (anim->var)
			lv_obj_delete_async(static_cast<lv_obj_t*>(anim->var));
		});
	lv_anim_start(&a);

	// 值变化时才保存
	if (lv_slider_get_value(m_volumeSlider) != m_volumeOnOpen)
		Audio::saveVolumeToNvs();
	m_volumeSlider = nullptr;
	ESP_LOGI(TAG, "音量滑块收起");
}

void DesktopApp::onVolumeSliderCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	int val = lv_slider_get_value(self->m_volumeSlider);
	Audio::setMasterVolume(val);
}

// ════════════════════════════════════════════════════════════════
// 状态栏图标回调
// ════════════════════════════════════════════════════════════════

void DesktopApp::onVolumeLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = 3;
	if (self->m_volumeSliderActive)
		self->hideVolumeSlider();
	else
		self->showVolumeSlider();
	self->applyFocus();
}

void DesktopApp::onBrightnessLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = 4;
	if (self->m_brightnessSliderActive)
		self->hideBrightnessSlider();
	else
		self->showBrightnessSlider();
	self->applyFocus();
}

void DesktopApp::onBatteryLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = 5;

	Task::addTask([](void* param) -> TickType_t
		{
			auto* app = static_cast<DesktopApp*>(param);
			if (app->getManager()) {
				auto* powerApp = new PowerManagementApp(app->getDisplay());
				app->getManager()->pushToNewStack(powerApp);
			}
			return Task::infinityTime;
		}, "openPowerMgr", self, 0, Task::Affinity::NotAssigned);
}

void DesktopApp::updateBatteryIcon()
{
	if (!m_batteryHandle) return;

	float capacity = 0;
	if (adc_battery_estimation_get_capacity(
			(adc_battery_estimation_handle_t)m_batteryHandle, &capacity) != ESP_OK)
		return;

	int pct = (int)(capacity + 0.5f);

	const char* icon;
	if (pct >= 80)       icon = LV_SYMBOL_BATTERY_FULL;
	else if (pct >= 60)  icon = LV_SYMBOL_BATTERY_3;
	else if (pct >= 40)  icon = LV_SYMBOL_BATTERY_2;
	else if (pct >= 20)  icon = LV_SYMBOL_BATTERY_1;
	else                 icon = LV_SYMBOL_BATTERY_EMPTY;

	lv_color_t color;
	if (pct >= 61)       color = GUI::Color::SUCCESS;
	else if (pct >= 21)  color = GUI::Color::WARNING;
	else                 color = GUI::Color::DANGER;

	lv_label_set_text(m_batteryLabel, icon);
	lv_obj_set_style_text_color(m_batteryLabel, color, 0);
}
