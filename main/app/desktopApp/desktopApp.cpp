#include "desktopApp.hpp"
#include "gui/gui.hpp"
#include "app/appStackManager.hpp"
#include "app/testApp/testApp.hpp"
#include "app/bleSettingsApp/bleSettingsApp.hpp"
#include "app/snake/snakeRoom/snakeRoom.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "display/font.hpp"

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
	"F:system/desktop/snake.png",
	"F:system/desktop/fruitNinja.png",
	"F:system/desktop/tetris.png",
	"F:system/desktop/douDiZhu.png",
	"F:system/desktop/chess.png",
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

	auto time_label = GUI::createLabel(status_row, "21:44");
	lv_obj_set_style_text_color(time_label, GUI::Color::TEXT, 0);
	lv_obj_set_flex_grow(time_label, 1);

	m_wifiLabel = GUI::createLabel(status_row, LV_SYMBOL_WIFI);
	lv_obj_set_style_text_color(m_wifiLabel, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(m_wifiLabel, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(m_wifiLabel, 16, 0);
	lv_obj_add_flag(m_wifiLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_wifiLabel, [](lv_event_t* e) {
		auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
		self->m_focusGroup = FOCUS_STATUS;
		self->m_focusStatusIdx = 0;
		ESP_LOGI(TAG, "WiFi 设置（待实现）");
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
	lv_obj_set_style_text_color(m_batteryLabel, GUI::Color::SUCCESS, 0);
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
			self->updateSelectionLabels();
			self->applyFocus();
			}, LV_EVENT_CLICKED, this);

		// 游戏图标（从 FLASH 加载，百分比尺寸随卡片自动缩放）
		auto img = lv_image_create(card);
		lv_image_set_src(img, GAME_ICONS[i]);
		lv_obj_set_size(img, lv_pct(100), lv_pct(100));
		lv_obj_center(img);

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
		lv_obj_t* targets[] = { m_wifiLabel, m_bluetoothLabel, m_volumeLabel, m_brightnessLabel, m_batteryLabel };
		if (m_focusStatusIdx >= 0 && m_focusStatusIdx < 5)
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
		switch (m_focusStatusIdx)
		{
		case 0: // WiFi — TODO: 打开 WiFi 设置
			ESP_LOGI(TAG, "WiFi 设置（待实现）");
			break;
		case 1: // 蓝牙 — 打开 BLE 设置
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
		case 2: // 音量
			ESP_LOGI(TAG, "音量调节（待实现）");
			break;
		case 3: // 亮度
			ESP_LOGI(TAG, "亮度调节（待实现）");
			break;
		case 4: // 电池/电源
			ESP_LOGI(TAG, "电源管理（待实现）");
			break;
		}
		break;
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

	if (m_focusCardsIdx == 0)
	{
		// 贪吃蛇 — 通过 AppStack 启动
		auto* snake = new SnakeRoom(display);
		m_manager->pushToNewStack(snake);
	}
	else
	{
		// 其他游戏 — TestApp 占位
		auto* testApp = new TestApp(display, esp_random());
		m_manager->pushToNewStack(testApp);
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
		break;

	case FOCUS_STATUS:
		if (lxLeft && m_focusStatusIdx > 0) {
			m_focusStatusIdx--;
			applyFocus();
		}
		if (lxRight && m_focusStatusIdx < 4) {
			m_focusStatusIdx++;
			applyFocus();
		}
		if (lyDown) navFromStatusDown();
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

// ════════════════════════════════════════════════════════════════
// 状态栏图标回调
// ════════════════════════════════════════════════════════════════

void DesktopApp::onVolumeLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = 2;
	ESP_LOGI(TAG, "音量调节（待实现）");
}

void DesktopApp::onBrightnessLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = 3;
	ESP_LOGI(TAG, "亮度调节（待实现）");
}

void DesktopApp::onBatteryLabelCb(lv_event_t* e)
{
	auto* self = static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self->m_focusGroup = FOCUS_STATUS;
	self->m_focusStatusIdx = 4;
	ESP_LOGI(TAG, "电源管理（待实现）");
}
