#include "display/display.hpp"
#include "touch/touch.hpp"
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "task/task.hpp"
#include "display/display.hpp"
#include "gui/gui.hpp"

static constexpr char TAG[] = "main";

// ========== 游戏数据 ==========

static const char* GAME_NAMES[] = {
	"Game 1",
	"Game 2",
	"Game 3",
	"Game 4",
	"Game 5",
};

static const char* GAME_DESCS[] = {
	"An exciting adventure awaits!",
	"Test your puzzle-solving skills.",
	"Fast-paced racing action.",
	"Build and manage your world.",
	"Epic battles and strategy.",
};

static constexpr int GAME_COUNT = sizeof(GAME_NAMES) / sizeof(GAME_NAMES[0]);

// ========== 状态变量 ==========

static int selected_index = 0;
static lv_obj_t* game_cards[GAME_COUNT] = {};
static lv_obj_t* desc_label = nullptr;
static lv_obj_t* info_label = nullptr;

// ========== 尺寸常量 ==========

static constexpr int ICON_W = 180;
static constexpr int ICON_H = 180;
static constexpr int ICON_SELECTED_W = 200;
static constexpr int ICON_SELECTED_H = 200;

// ========== 状态行对象 ==========

static lv_obj_t* wifi_label = nullptr;
static lv_obj_t* bluetooth_label = nullptr;
static lv_obj_t* battery_label = nullptr;

/**
 * @brief 更新状态行显示
 */
static void update_status_bar()
{
	// 模拟 WiFi 信号强度（实际可从 wifi 模块获取）
	static int wifi_bars = 3;
	lv_label_set_text(wifi_label, wifi_bars >= 3 ? "\xEF\x87\xAB" :
	                   wifi_bars >= 1 ? "\xEF\x87\xA9" : "\xEF\x87\xA8");

	// 模拟蓝牙状态
	lv_label_set_text(bluetooth_label, "\xEF\x84\x99");

	// 模拟电池电量
	static int battery_pct = 85;
	lv_label_set_text(battery_label, battery_pct > 20 ? "\xEF\x89\x80" : "\xEF\x89\x84");
}

/**
 * @brief 创建顶部状态行（无背景框）
 */
static void create_status_bar(lv_obj_t* parent)
{
	auto status_row = GUI::createFlex(parent, LV_FLEX_FLOW_ROW,
	                                  lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_style_pad_all(status_row, 8, 0);
	lv_obj_set_style_pad_left(status_row, 16, 0);
	lv_obj_set_style_pad_right(status_row, 16, 0);
	lv_obj_set_style_border_width(status_row, 0, 0);
	lv_obj_set_style_bg_opa(status_row, LV_OPA_TRANSP, 0);
	lv_obj_align(status_row, LV_ALIGN_TOP_MID, 0, 0);

	auto time_label = GUI::createLabel(status_row, "21:44");
	lv_obj_set_style_text_color(time_label, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);
	lv_obj_set_flex_grow(time_label, 1);

	wifi_label = GUI::createLabel(status_row, "\xEF\x87\xAB");
	lv_obj_set_style_text_color(wifi_label, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_pad_right(wifi_label, 16, 0);

	bluetooth_label = GUI::createLabel(status_row, "\xEF\x84\x99");
	lv_obj_set_style_text_color(bluetooth_label, GUI::Color::TEXT, 0);
	lv_obj_set_style_text_font(bluetooth_label, &lv_font_montserrat_24, 0);
	lv_obj_set_style_pad_right(bluetooth_label, 16, 0);

	// 电池图标
	battery_label = GUI::createLabel(status_row, "\xEF\x89\x80");
	lv_obj_set_style_text_color(battery_label, GUI::Color::SUCCESS, 0);
	lv_obj_set_style_text_font(battery_label, &lv_font_montserrat_24, 0);

	update_status_bar();
}

/**
 * @brief 更新所有图标的选中状态
 */
static void update_selection()
{
	for (int i = 0; i < GAME_COUNT; i++) {
		if (!game_cards[i]) continue;

		bool is_selected = (i == selected_index);

		// 选中：放大 + 高亮边框
		lv_obj_set_size(game_cards[i],
			is_selected ? ICON_SELECTED_W : ICON_W,
			is_selected ? ICON_SELECTED_H : ICON_H);

		if (is_selected) {
			lv_obj_set_style_border_width(game_cards[i], 3, 0);
			lv_obj_set_style_border_color(game_cards[i], GUI::Color::PRIMARY, 0);
			lv_obj_set_style_shadow_width(game_cards[i], 16, 0);
			lv_obj_set_style_shadow_color(game_cards[i], GUI::Color::PRIMARY, 0);
			lv_obj_set_style_shadow_opa(game_cards[i], LV_OPA_60, 0);
		} else {
			lv_obj_set_style_border_width(game_cards[i], 0, 0);
			lv_obj_set_style_shadow_width(game_cards[i], 8, 0);
			lv_obj_set_style_shadow_color(game_cards[i], lv_color_hex(0x000000), 0);
			lv_obj_set_style_shadow_opa(game_cards[i], LV_OPA_50, 0);
		}
	}

	// 更新底部描述和选中信息
	if (desc_label) {
		lv_label_set_text(desc_label, GAME_DESCS[selected_index]);
	}
	if (info_label) {
		lv_label_set_text_fmt(info_label, "Selected: %s", GAME_NAMES[selected_index]);
	}
}

/**
 * @brief 选择下一个游戏（右移）
 */
static void select_next()
{
	selected_index = (selected_index + 1) % GAME_COUNT;
	update_selection();
	ESP_LOGI(TAG, "Selected: %s (index=%d)", GAME_NAMES[selected_index], selected_index);
}

/**
 * @brief 选择上一个游戏（左移）
 */
static void select_prev()
{
	selected_index = (selected_index - 1 + GAME_COUNT) % GAME_COUNT;
	update_selection();
	ESP_LOGI(TAG, "Selected: %s (index=%d)", GAME_NAMES[selected_index], selected_index);
}

/**
 * @brief 按钮事件回调：右移
 */
static void btn_next_cb(lv_event_t* e)
{
	select_next();
}

/**
 * @brief 按钮事件回调：左移
 */
static void btn_prev_cb(lv_event_t* e)
{
	select_prev();
}

/**
 * @brief "开始游戏"按钮回调
 */
static void btn_start_cb(lv_event_t* e)
{
	ESP_LOGI(TAG, "Starting game: %s", GAME_NAMES[selected_index]);
}

extern "C" void app_main(void)
{
	// 1. 初始化显示（硬件 + LVGL 适配器）
	Display display;
	if (!display.init(ESP_LV_ADAPTER_ROTATE_90)) {
		ESP_LOGE(TAG, "Failed to initialize display");
		return;
	}

	IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	Touch touch{ iic, {GPIO_NUM_46} };
	display.bindTouch(touch.getHandle());

	// (可选) 在此处注册触摸/按钮等输入设备
	// ...

	// 2. 启动 LVGL 工作任务
	if (!display.start()) {
		ESP_LOGE(TAG, "Failed to start LVGL adapter");
		return;
	}

	// 3. 使用 LVGL API 绘制界面（RAII 自动加锁/解锁）
	if (auto guard = display.lockGuard())
	{
		label = lv_label_create(lv_scr_act());
		lv_label_set_text(label, "Hello LVGL!");
		lv_obj_center(label);

		auto screen = lv_screen_active();
		lv_obj_add_event_cb(screen, [](lv_event_t* event)
			{
				ESP_LOGI("callback", "LV_EVENT_PRESSED");
			}, LV_EVENT_PRESSED, nullptr);
		lv_obj_add_event_cb(screen, [](lv_event_t* event)
			{
				ESP_LOGI("callback", "LV_EVENT_PRESSING");
			}, LV_EVENT_PRESSING, nullptr);
		lv_obj_add_event_cb(screen, [](lv_event_t* event)
			{
				ESP_LOGI("callback", "LV_EVENT_RELEASED");
			}, LV_EVENT_RELEASED, nullptr);
	} // guard 析构时自动解锁

		// ========== 顶部状态行（无黑框） ==========
		create_status_bar(page);

		// ========== 页面标题 ==========
		auto title = GUI::createTitle(page, "Game Center");
		lv_obj_set_style_text_color(title, GUI::Color::TEXT, 0);
		lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 50);

		// ========== 中间一行放五个游戏图标（略微上移） ==========
		auto flex = GUI::createFlex(page, LV_FLEX_FLOW_ROW,
		                            LV_SIZE_CONTENT, LV_SIZE_CONTENT);
		lv_obj_set_style_pad_all(flex, 10, 0);
		lv_obj_set_style_pad_column(flex, 16, 0);
		lv_obj_align(flex, LV_ALIGN_CENTER, 0, -50);

		// 创建五个白色游戏图标卡片（直角）
		for (int i = 0; i < GAME_COUNT; i++) {
			auto card = GUI::createCard(flex, ICON_W, ICON_H);
			lv_obj_set_style_radius(card, 0, 16);
			lv_obj_set_style_bg_color(card, LV_COLOR_MAKE(0xFF, 0xFF, 0xFF), 0); // 白色
			lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
			lv_obj_set_style_pad_all(card, 0, 0);

			// 图标内部：用 label 显示游戏名（深色文字）
			auto label = GUI::createLabel(card, GAME_NAMES[i]);
			lv_obj_set_style_text_color(label, LV_COLOR_MAKE(0x1A, 0x1A, 0x2E), 0);
			lv_obj_center(label);
			lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

			game_cards[i] = card;
		}

		// 初始化选中状态（默认选中第一个）
		update_selection();

		// ========== 左右切换按钮 ==========
		auto btn_prev = GUI::createButton(page, "<", 60, 60);
		lv_obj_align(btn_prev, LV_ALIGN_LEFT_MID, 20, -50);
		lv_obj_add_event_cb(btn_prev, btn_prev_cb, LV_EVENT_CLICKED, nullptr);

		auto btn_next = GUI::createButton(page, ">", 60, 60);
		lv_obj_align(btn_next, LV_ALIGN_RIGHT_MID, -20, -50);
		lv_obj_add_event_cb(btn_next, btn_next_cb, LV_EVENT_CLICKED, nullptr);

		// ========== 底部区域：游戏说明 + 开始按钮 ==========
		auto bottom_area = GUI::createFlex(page, LV_FLEX_FLOW_COLUMN,
		                                   lv_pct(80), LV_SIZE_CONTENT);
		lv_obj_set_style_pad_all(bottom_area, 16, 0);
		lv_obj_set_style_border_width(bottom_area, 0, 0);
		lv_obj_set_style_bg_opa(bottom_area, LV_OPA_TRANSP, 0);
		lv_obj_align(bottom_area, LV_ALIGN_BOTTOM_MID, 0, -40);

		// 游戏简要说明
		desc_label = GUI::createLabel(bottom_area, GAME_DESCS[selected_index]);
		lv_obj_set_style_text_color(desc_label, GUI::Color::SUBTLE, 0);
		lv_obj_set_style_text_align(desc_label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_width(desc_label, lv_pct(100));

		// 间距
		auto spacer = GUI::createLabel(bottom_area, "");
		lv_obj_set_height(spacer, 16);
		lv_obj_set_style_border_width(spacer, 0, 0);
		lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);

		// "开始游戏" 按钮
		auto btn_start = GUI::createButton(bottom_area, "Start Game", 200, 50);
		lv_obj_set_style_radius(btn_start, 25, 0);
		lv_obj_set_style_bg_color(btn_start, GUI::Color::SUCCESS, 0);
		lv_obj_add_event_cb(btn_start, btn_start_cb, LV_EVENT_CLICKED, nullptr);

		// 底部选中信息
		info_label = GUI::createSubtitle(page, "Selected: Game 1");
		lv_obj_align(info_label, LV_ALIGN_BOTTOM_MID, 0, -5);
		lv_obj_set_style_text_color(info_label, GUI::Color::PRIMARY, 0);

		ESP_LOGI(TAG, "Home page created with dark theme");
	}

	while (true) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
}
