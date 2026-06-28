#include "desktopApp.hpp"
#include "app/desktopApp/gui.hpp"
#include "app/appStackManager.hpp"
#include "app/testApp/testApp.hpp"
#include "gamepadIndev/gamepadIndev.hpp"
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

void DesktopApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "Failed to lock display");
		return;
	}

	// 设置页面背景
	lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_top(screen, 0, 0);
	lv_obj_set_style_pad_left(screen, 16, 0);
	lv_obj_set_style_pad_right(screen, 16, 0);

	// 顶部状态行
	create_status_bar();

	// 页面标题
	auto title = GUI::createTitle(screen, "游戏中心");
	lv_obj_set_style_text_color(title, GUI::Color::TEXT, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

	// 中间一行放五个游戏图标
	auto flex = GUI::createFlex(screen, LV_FLEX_FLOW_ROW, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(flex, 10, 0);
	lv_obj_set_style_pad_column(flex, 16, 0);
	lv_obj_align(flex, LV_ALIGN_CENTER, 0, -50);

	// 创建五个游戏图标卡片
	for (int i = 0; i < GAME_COUNT; i++)
	{
		auto card = GUI::createCard(flex, ICON_W, ICON_H);
		lv_obj_set_style_radius(card, 0, 16);
		lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), 0);
		lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
		lv_obj_set_style_pad_all(card, 0, 0);

		auto label = GUI::createLabel(card, GAME_NAMES[i]);
		lv_obj_set_style_text_color(label, LV_COLOR_MAKE(0x1A, 0x1A, 0x2E), 0);
		lv_obj_center(label);
		lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

		game_cards[i] = card;
	}

	// 初始化选中状态
	update_selection();

	// 创建 LVGL 焦点组 — 所有卡片共享
	m_group = lv_group_create();
	for (int i = 0; i < GAME_COUNT; i++)
	{
		lv_group_add_obj(m_group, game_cards[i]);
		lv_obj_add_event_cb(game_cards[i], on_card_key_cb, LV_EVENT_KEY, this);
	}
	// 绑定所有玩家到桌面共享组（系统界面不区分玩家）
	for (uint8_t i = 0; i < MaxPlayers; i++)
		GamepadIndev::instance().bindGroup(i, m_group);
	// 设置初始焦点
	lv_group_focus_obj(game_cards[selected_index]);

	// 左右切换按钮
	auto btn_prev = GUI::createButton(screen, "<", 60, 60);
	lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 20, -50);
	lv_obj_add_event_cb(btn_prev, btn_prev_cb, LV_EVENT_CLICKED, this);

	auto btn_next = GUI::createButton(screen, ">", 60, 60);
	lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, -20, -50);
	lv_obj_add_event_cb(btn_next, btn_next_cb, LV_EVENT_CLICKED, this);

	// 底部区域：游戏说明 + 开始按钮
	auto bottom_area = GUI::createFlex(screen, LV_FLEX_FLOW_COLUMN, lv_pct(80), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(bottom_area, 16, 0);
	lv_obj_set_style_border_width(bottom_area, 0, 0);
	lv_obj_set_style_bg_opa(bottom_area, LV_OPA_TRANSP, 0);
	lv_obj_align(bottom_area, LV_ALIGN_BOTTOM_MID, 0, -40);

	desc_label = GUI::createLabel(bottom_area, "");
	lv_obj_set_style_text_color(desc_label, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(desc_label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);

	info_label = GUI::createLabel(bottom_area, "");
	lv_obj_set_style_text_color(info_label, GUI::Color::TEXT, 0);

	auto btn_start = GUI::createButton(bottom_area, "开始游戏", 160, 50);
	lv_obj_add_event_cb(btn_start, btn_start_cb, LV_EVENT_CLICKED, this);
	lv_obj_set_style_radius(btn_start, 12, 0);
}

void DesktopApp::deinit()
{
	if (m_group)
	{
		auto guard = display->lockGuard();
		lv_group_delete(m_group);
		m_group = nullptr;
	}
	App::deinit();
}

void DesktopApp::onForeground()
{
	// 恢复焦点到当前选中项
	nextAppChangeTime = xTaskGetTickCount() + 500;
	if (m_group && game_cards[selected_index])
	{
		ESP_LOGI(TAG, "lockGuard to focus on game card %d", selected_index);
		auto guard = display->lockGuard();
		ESP_LOGI(TAG, "Focus on game card %d", selected_index);
		lv_group_focus_obj(game_cards[selected_index]);
		ESP_LOGI(TAG, "Focus done");
	}
}

void DesktopApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	// 动作键（ENTER/HOME→start）从 BLE task 路径走，避免在 LVGL 定时器上下文中切屏
	if (state.isPressed(GamepadButton::BTN_A) || state.isPressed(GamepadButton::BTN_L3))
	{
		start();
	}
}

void DesktopApp::update_selection()
{
	// 注意：此函数可能从 LVGL 事件回调 / LVGL 定时器上下文中调用
	// 此时已持有 LVGL 锁，不可再调用 lockGuard()
	for (int i = 0; i < GAME_COUNT; i++)
	{
		if (!game_cards[i]) continue;

		bool is_selected = (i == selected_index);

		lv_obj_set_size(game_cards[i],
			is_selected ? ICON_SELECTED_W : ICON_W,
			is_selected ? ICON_SELECTED_H : ICON_H);

		if (is_selected)
		{
			lv_obj_set_style_border_width(game_cards[i], 3, 0);
			lv_obj_set_style_border_color(game_cards[i], GUI::Color::PRIMARY, 0);
			lv_obj_set_style_shadow_width(game_cards[i], 16, 0);
			lv_obj_set_style_shadow_color(game_cards[i], GUI::Color::PRIMARY, 0);
			lv_obj_set_style_shadow_opa(game_cards[i], LV_OPA_60, 0);
		}
		else
		{
			lv_obj_set_style_border_width(game_cards[i], 0, 0);
			lv_obj_set_style_shadow_width(game_cards[i], 8, 0);
			lv_obj_set_style_shadow_color(game_cards[i], lv_color_hex(0x000000), 0);
			lv_obj_set_style_shadow_opa(game_cards[i], LV_OPA_50, 0);
		}
	}

	if (desc_label)
	{
		lv_label_set_text(desc_label, GAME_DESCS[selected_index]);
	}
	if (info_label)
	{
		lv_label_set_text_fmt(info_label, "已选择: %s", GAME_NAMES[selected_index]);
	}
}

void DesktopApp::create_status_bar()
{
	auto status_row = GUI::createFlex(screen, LV_FLEX_FLOW_ROW,
		lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(status_row, 8, 0);
	lv_obj_set_style_pad_left(status_row, 16, 0);
	lv_obj_set_style_pad_right(status_row, 16, 0);
	lv_obj_set_style_border_width(status_row, 0, 0);
	lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
	lv_obj_align(status_row, LV_ALIGN_TOP_MID, 0, 0);

	auto time_label = GUI::createLabel(status_row, "21:44");
	lv_obj_set_style_text_color(time_label, GUI::Color::TEXT, 0);
	lv_obj_set_flex_grow(time_label, 1);

	wifi_label = GUI::createLabel(status_row, "\xEF\x87\xAB");
	lv_obj_set_style_text_color(wifi_label, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(wifi_label, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(wifi_label, 16, 0);

	bluetooth_label = GUI::createLabel(status_row, "\xEF\x84\x99");
	lv_obj_set_style_text_color(bluetooth_label, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(bluetooth_label, LV_FONT_DEFAULT, 0);
	lv_obj_set_style_pad_right(bluetooth_label, 16, 0);

	battery_label = GUI::createLabel(status_row, "\xEF\x89\x80");
	lv_obj_set_style_text_color(battery_label, GUI::Color::SUCCESS, 0);
	lv_obj_set_style_text_font(battery_label, LV_FONT_DEFAULT, 0);
}

void DesktopApp::btn_next_cb(lv_event_t* e)
{
	auto self = *static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self.next();
}

void DesktopApp::btn_prev_cb(lv_event_t* e)
{
	auto self = *static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self.previous();
}

void DesktopApp::btn_start_cb(lv_event_t* e)
{
	auto self = *static_cast<DesktopApp*>(lv_event_get_user_data(e));
	self.start();
}

void DesktopApp::on_card_key_cb(lv_event_t* e)
{
	auto self = *static_cast<DesktopApp*>(lv_event_get_user_data(e));
	uint32_t key = lv_event_get_key(e);

	switch (key)
	{
	case LV_KEY_LEFT:
		self.previous();
		break;
	case LV_KEY_RIGHT:
		self.next();
		break;
	default:
		break;
	}
}

void DesktopApp::next()
{
	selected_index = (selected_index + 1) % GAME_COUNT;
	ESP_LOGI(TAG, "已选择: %s (index=%d)", GAME_NAMES[selected_index], selected_index);
}

void DesktopApp::previous()
{
	selected_index = (selected_index - 1 + GAME_COUNT) % GAME_COUNT;
	ESP_LOGI(TAG, "已选择: %s (index=%d)", GAME_NAMES[selected_index], selected_index);
}

void DesktopApp::start()
{
	if (xTaskGetTickCount() < nextAppChangeTime)
	{
		ESP_LOGI(TAG, "start 被忽略，等待 %d 毫秒", (nextAppChangeTime - xTaskGetTickCount()) * portTICK_PERIOD_MS);
		return;
	}

	ESP_LOGI(TAG, "启动游戏: %s", GAME_NAMES[selected_index]);

	// 使用 pushToNewStack 为游戏创建独立调用栈
	if (m_manager)
	{
		auto* testApp = new TestApp(display, esp_random());
		m_manager->pushToNewStack(testApp);
	}
	else
	{
		ESP_LOGE(TAG, "btn_start_cb: no manager set");
	}
}
