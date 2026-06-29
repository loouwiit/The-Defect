#include "desktopApp.hpp"
#include "app/desktopApp/gui.hpp"
#include "app/appStackManager.hpp"
#include "app/testApp/testApp.hpp"
#include "app/bleSettingsApp/bleSettingsApp.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/font.hpp"

// ========== 游戏数据 ==========

const char* DesktopApp::GAME_NAMES[] =
{
	"游戏 1",
	"游戏 2",
	"游戏 3",
	"游戏 4",
	"游戏 5",
};

const char* DesktopApp::GAME_DESCS[] =
{
	"一场激动人心的冒险等待着你！",
	"测试你的解谜能力。",
	"快节奏的赛车体验。",
	"建造和管理你的世界。",
	"史诗般的战斗和策略。",
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

	// 初始聚焦
	m_focusGroup = FOCUS_CARDS;
	m_focusCardsIdx = 0;
	applyFocus();

	ESP_LOGI(TAG, "桌面初始化完成");
}

void DesktopApp::deinit()
{
	App::deinit();
	ESP_LOGI(TAG, "桌面已释放");
}

void DesktopApp::onForeground()
{
	// 防抖重置
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	for (auto& i : m_nextMoveTime) i = 0;
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
	// screen 设为 flex 列布局
	lv_obj_set_layout(screen, LV_LAYOUT_FLEX);
	lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// ── 顶部状态行 ──
	auto status_row = GUI::createFlex(screen, LV_FLEX_FLOW_ROW,
		lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(status_row, 8, 0);
	lv_obj_set_style_pad_left(status_row, 16, 0);
	lv_obj_set_style_pad_right(status_row, 16, 0);
	lv_obj_set_style_border_width(status_row, 0, 0);
	lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);

	auto time_label = GUI::createLabel(status_row, "21:44");
	lv_obj_set_style_text_color(time_label, GUI::Color::TEXT, 0);
	lv_obj_set_flex_grow(time_label, 1);

	m_wifiLabel = GUI::createLabel(status_row, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_color(m_wifiLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_wifiLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_wifiLabel, 16, 0);

	m_bluetoothLabel = GUI::createLabel(status_row, LV_SYMBOL_BLUETOOTH);
	lv_obj_set_style_text_color(m_bluetoothLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_bluetoothLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_bluetoothLabel, 16, 0);
	lv_obj_add_flag(m_bluetoothLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_bluetoothLabel, onBluetoothLabelCb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_border_width(m_bluetoothLabel, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_bluetoothLabel, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_border_side(m_bluetoothLabel, LV_BORDER_SIDE_FULL, LV_STATE_FOCUSED);

	m_batteryLabel = GUI::createLabel(status_row, LV_SYMBOL_BATTERY_FULL);
	lv_obj_set_style_text_color(m_batteryLabel, GUI::Color::SUCCESS, 0);
	lv_obj_set_style_text_font(m_batteryLabel, LV_FONT_DEFAULT, 0);

	// ── 页面标题（居中） ──
	auto title = GUI::createTitle(screen, "游戏中心");
	lv_obj_set_width(title, lv_pct(100));
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_top(title, 20, 0);
	lv_obj_set_style_pad_bottom(title, 10, 0);

	// ── 卡片区域（flex_grow 填充中间） ──
	auto cards_section = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(cards_section, 1);
	lv_obj_set_style_pad_all(cards_section, 10, 0);
	lv_obj_set_style_pad_column(cards_section, 8, 0);
	lv_obj_set_style_border_width(cards_section, 0, 0);
	lv_obj_set_style_bg_opa(cards_section, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(cards_section, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 左箭头（触控备选，不参与焦点导航）
	lv_obj_set_style_pad_left(cards_section, 32, 0);
	m_prevBtn = GUI::createButton(cards_section, "<", 52, 52);
	lv_obj_set_style_radius(m_prevBtn, 30, 0);
	lv_obj_add_event_cb(m_prevBtn, onPrevBtnCb, LV_EVENT_CLICKED, this);

	// 卡片行
	m_cardsRow = GUI::createFlex(cards_section, LV_FLEX_FLOW_ROW, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_flex_grow(m_cardsRow, 1);
	lv_obj_set_style_pad_column(m_cardsRow, 16, 0);
	lv_obj_set_style_border_width(m_cardsRow, 0, 0);
	lv_obj_set_style_bg_opa(m_cardsRow, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(m_cardsRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 创建五个游戏卡片
	for (int i = 0; i < GAME_COUNT; i++)
	{
		auto card = GUI::createCard(m_cardsRow, CARD_W, CARD_H);
		lv_obj_set_style_radius(card, 16, 0);
		lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_all(card, 0, 0);
		lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_user_data(card, (void*)(uintptr_t)i);
		// ── 默认状态样式 ──
		lv_obj_set_style_border_width(card, 0, 0);
		lv_obj_set_style_shadow_width(card, 8, 0);
		lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
		lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);

		// ── 聚焦状态样式（LVGL 自动切换） ──
		lv_obj_set_style_border_width(card, 3, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(card, lv_color_white(), LV_STATE_FOCUSED);
		lv_obj_set_style_border_side(card, LV_BORDER_SIDE_FULL, LV_STATE_FOCUSED);
		lv_obj_set_style_shadow_width(card, 16, LV_STATE_FOCUSED);
		lv_obj_set_style_shadow_color(card, GUI::Color::PRIMARY, LV_STATE_FOCUSED);
		lv_obj_set_style_shadow_opa(card, LV_OPA_60, LV_STATE_FOCUSED);

		// 点击聚焦到该卡片
		lv_obj_add_event_cb(card, [](lv_event_t* e) {
			auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
			auto* c = static_cast<lv_obj_t*>(lv_event_get_target(e));
			self->m_focusGroup = FOCUS_CARDS;
			self->m_focusCardsIdx = (int8_t)(uintptr_t)lv_obj_get_user_data(c);
			self->updateCardSizes();
			self->updateSelectionLabels();
			self->applyFocus();
			}, LV_EVENT_CLICKED, this);

		auto label = GUI::createLabel(card, GAME_NAMES[i]);
		lv_obj_set_style_text_color(label, LV_COLOR_MAKE(0x1A, 0x1A, 0x2E), 0);
		lv_obj_center(label);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

		m_gameCards[i] = card;
	}

	// 右箭头（触控备选，不参与焦点导航）
	lv_obj_set_style_pad_right(cards_section, 32, 0);
	m_nextBtn = GUI::createButton(cards_section, ">", 52, 52);
	lv_obj_set_style_radius(m_nextBtn, 30, 0);
	lv_obj_add_event_cb(m_nextBtn, onNextBtnCb, LV_EVENT_CLICKED, this);

	// ── 底部区域（左简介 / 右信息+按钮） ──
	auto bottom_area = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, lv_pct(86), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(bottom_area, 12, 0);
	lv_obj_set_style_pad_bottom(bottom_area, 80, 0);
	lv_obj_set_style_pad_left(bottom_area, 80, 0);
	lv_obj_set_style_pad_right(bottom_area, 24, 0);
	lv_obj_set_style_border_width(bottom_area, 0, 0);
	lv_obj_set_style_bg_opa(bottom_area, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(bottom_area, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	// 左侧：游戏简介
	m_descLabel = GUI::createLabel(bottom_area, "");
	lv_obj_set_style_text_color(m_descLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_descLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_set_width(m_descLabel, lv_pct(40));
	lv_label_set_long_mode(m_descLabel, LV_LABEL_LONG_WRAP);

	// 右侧：已选择提示 + 开始按钮
	auto right_side = GUI::createFlex(bottom_area, LV_FLEX_FLOW_COLUMN, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_style_border_width(right_side, 0, 0);
	lv_obj_set_style_bg_opa(right_side, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_align(right_side, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	m_infoLabel = GUI::createLabel(right_side, "");
	lv_obj_set_style_text_color(m_infoLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_pad_bottom(m_infoLabel, 8, 0);

	m_startBtn = GUI::createButton(right_side, "开始游戏", 160, 50);
	lv_obj_set_style_radius(m_startBtn, 12, 0);
	lv_obj_set_style_border_width(m_startBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(m_startBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(m_startBtn, onStartBtnCb, LV_EVENT_CLICKED, this);

	// 初始化卡片状态
	updateCardSizes();
	updateSelectionLabels();
	applyFocus();
}

// ════════════════════════════════════════════════════════════════
// 卡片尺寸更新
// ════════════════════════════════════════════════════════════════

void DesktopApp::updateCardSizes()
{
	for (int i = 0; i < GAME_COUNT; i++)
	{
		if (!m_gameCards[i]) continue;
		bool sel = (i == m_focusCardsIdx);
		lv_obj_set_size(m_gameCards[i],
			sel ? CARD_SEL_W : CARD_W,
			sel ? CARD_SEL_H : CARD_H);
	}
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

	clearFocus(m_bluetoothLabel);
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
			// 滚动到视图
			lv_obj_scroll_to_view(m_gameCards[m_focusCardsIdx], LV_ANIM_ON);
		}
		break;

	case FOCUS_BOTTOM:
		focus(m_startBtn);
		break;

	case FOCUS_STATUS:
		focus(m_bluetoothLabel);
		break;
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
		// 打开 BLE 设置（LVGL 事件回调中持锁，须延后）
		Task::addTask([](void* param) -> TickType_t
			{
				auto* app = static_cast<DesktopApp*>(param);
				if (app->getManager()) {
					auto* bleApp = new BleSettingsApp(app->getDisplay());
					app->getManager()->pushToNewStack(bleApp);
				}
				return Task::infinityTime;
			}, "openBleSettings", this, 0, Task::Affinity::None);
		break;
	}
}

// ── 卡片导航 ──

void DesktopApp::navCardsLeft()
{
	auto guard = display->lockGuard();
	m_focusCardsIdx = (m_focusCardsIdx - 1 + GAME_COUNT) % GAME_COUNT;
	updateCardSizes();
	updateSelectionLabels();
	applyFocus();
	ESP_LOGI(TAG, "已选择: %s (index=%d)", GAME_NAMES[m_focusCardsIdx], m_focusCardsIdx);
}

void DesktopApp::navCardsRight()
{
	auto guard = display->lockGuard();
	m_focusCardsIdx = (m_focusCardsIdx + 1) % GAME_COUNT;
	updateCardSizes();
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

	if (m_manager)
	{
		auto* testApp = new TestApp(display, esp_random());
		m_manager->pushToNewStack(testApp);
	}
	else
	{
		ESP_LOGE(TAG, "startGame: no manager set");
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

	// ── BTN_B: 根栈禁止 pop，忽略 ──
	// if (state.isPressed(GamepadButton::BTN_B))
	// {
	// }

	// ── 激活 (BTN_A / BTN_L3) ──
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
	case FOCUS_CARDS:
		if (lxLeft)  navCardsLeft();
		if (lxRight) navCardsRight();
		if (lyUp)    navToStatus();
		if (lyDown)  navToBottom();
		break;

	case FOCUS_BOTTOM:
		if (lyUp) navFromBottomUp();
		if (lyDown) navToBottom(); // 无动作，保留在底部
		break;

	case FOCUS_STATUS:
		if (lyDown) navFromStatusDown();
		if (lyUp) navToStatus(); // 无动作，保留在状态栏
		break;
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
