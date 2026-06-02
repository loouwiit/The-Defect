#include "display/display.hpp"
#include "display/ili9881c.hpp"
#include "display/font.hpp"
#include "touch/touch.hpp"

#include <esp_log.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_event.h>
#include <esp_err.h>


#include "task/task.hpp"
#include "wifi/nvs.hpp"
#include "wifi/wifi.hpp"
#include "server/serverKernal.hpp"

#include "storage/fat.hpp"
#include "storage/mem.hpp"
#include "storage/sd.hpp"

#include "app/desktopApp/desktopApp.hpp"
#include "screenStream/screenStream.hpp"
#include "virtualIndev/virtualIndev.hpp"
#include "wsServer/wsServer.hpp"
#include "wifi/mdns.hpp"

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
	Task::init(2);

	ESP_LOGI(TAG, "esp_event_loop_create_default");
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	// 存储
	mountFlash();
	mountMem();
	mountSd();

	// 计算帧缓冲区数量
	constexpr auto tearAvoidMode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_DEFAULT_MIPI_DSI;
	constexpr auto rotation = ESP_LV_ADAPTER_ROTATE_90;
	const auto frameBufferCount = esp_lv_adapter_get_required_frame_buffer_count(tearAvoidMode, rotation);

	constexpr auto horizontalResolution = 720;
	constexpr auto verticalResolution = 1280;

	// 初始化LVGL
	Display display;
	if (!display.init()) {
		ESP_LOGE(TAG, "Failed to initialize display");
		return;
	}

	// 初始化ILI9881c
	if (!ILI9881c::getInstance().init(horizontalResolution, verticalResolution, frameBufferCount))
	{
		ESP_LOGE(TAG, "Failed to initialize ILI9881c hardware");
		return;
	}

	display.bindDisplay(ILI9881c::getInstance().getPanel(), ILI9881c::getInstance().getPanelIo(), horizontalResolution, verticalResolution, tearAvoidMode, rotation);

	// IIC iic{ {GPIO_NUM_8}, {GPIO_NUM_7} };
	// Touch touch{ iic, {GPIO_NUM_46} };
	// display.bindTouch(touch.getHandle());

	// 启动 LVGL 工作任务
	if (!display.start()) {
		ESP_LOGE(TAG, "Failed to start LVGL adapter");
		return;
	}
	display.setFpsStatisticsEnabled();

	// 启动屏幕流模块（用于 HTTP MJPEG 流）
	ScreenStream::instance().start(&display, horizontalResolution, verticalResolution, true, 1);

	// 加载字体并设为默认
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Default));
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Large), FontLoader::FontSize::Large);
	FontLoader::setDefault(FontLoader::load("F:system/NotoSC.ttf", (int)FontLoader::FontSize::Small), FontLoader::FontSize::Small);

	// 启动虚拟触摸输入（用于从 web 注入触摸事件）
	VirtualIndev::instance().start(&display);

	// 启动任务管理器
	Task::init(2);

	// 启动桌面应用
	DesktopApp* app = new DesktopApp{ &display };
	app->init();
	if (auto guard = display.lockGuard())
	{
		// 应用(v.) 应用(n.) 到屏幕
		display.applyApp(app);
	}

	// wifi
	nvsInit();
	wifiInit(true);

	wifiStart();
	auto sta = wifiStationGetInfo();
	wifiStationStart();
	wifiConnect((const char*)sta.ssid, (const char*)sta.password);
	while (wifiIsWantConnect() && !wifiIsConnect())
		vTaskDelay(100);

	if (!wifiIsConnect())
	{
		// 关闭wifi启动AP
		wifiStationStop();
		auto ap = wifiApGetInfo();
		ESP_LOGI(TAG, "load ap ssid: %s, password: %s", sta.ssid, sta.password);
		if (ap.ssid_len == 0)
			wifiApSet("esp32p4", "12345678");
		wifiApStart();
	}

	// mdns
	ESP_LOGI(TAG, "mdnsInit");
	mdnsInit();
	mdnsStart("esp32p4", "ESP32P4 Game Console");
	ESP_LOGI(TAG, "mdnsInit done");

	ESP_LOGI(TAG, "mdnsServiceAdd");
	mdnsServiceAdd("ESP32P4 HTTP", "_http", "_tcp", 80);
	mdnsServiceAdd("ESP32P4 WS", "_ws", "_tcp", 8080);
	ESP_LOGI(TAG, "mdnsServiceAdd done");

	// 服务器
	ESP_LOGI(TAG, "serverStart");
	serverStart();
	ESP_LOGI(TAG, "serverStart done");

	// WebSocket 服务器（端口 8080，仅用于触控回传）
	ESP_LOGI(TAG, "wsServerStart");
	wsServerStart();
	ESP_LOGI(TAG, "wsServerStart done");

	// 保持栈上变量，后续移除
	while (true)
		vTaskDelay(1000);

	// cleanup (unreachable in this example, but good practice)
	delete std::exchange(app, nullptr);
}
