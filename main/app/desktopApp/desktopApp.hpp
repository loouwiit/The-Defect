#pragma once

#include "app/app.hpp"
#include <cstdint>

class DesktopApp : public App
{
public:
	DesktopApp(Display* display);
	~DesktopApp() override;

	void init() override;
	void deinit() override;

private:
	// 游戏数据
	static constexpr int GAME_COUNT = 5;
	static const char* GAME_NAMES[GAME_COUNT];
	static const char* GAME_DESCS[GAME_COUNT];
	static const char* GAME_ICONS[GAME_COUNT];

	// UI 对象
	int selected_index = 0;
	lv_obj_t* game_cards[GAME_COUNT] = {};
	lv_obj_t* desc_label = nullptr;
	lv_obj_t* info_label = nullptr;
	lv_obj_t* wifi_label = nullptr;
	lv_obj_t* bluetooth_label = nullptr;
	lv_obj_t* battery_label = nullptr;

	// 尺寸常量
	static constexpr int ICON_W = 180;
	static constexpr int ICON_H = 180;
	static constexpr int ICON_SELECTED_W = 200;
	static constexpr int ICON_SELECTED_H = 200;

	// 私有方法
	void update_selection();
	void create_status_bar();
	static void btn_next_cb(lv_event_t* e);
	static void btn_prev_cb(lv_event_t* e);
	static void btn_start_cb(lv_event_t* e);
};
