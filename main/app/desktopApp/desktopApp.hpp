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
	lv_obj_t* m_btnPower = nullptr;
	lv_obj_t* m_btnBrightness = nullptr;
	lv_obj_t* m_btnSettings = nullptr;

	// 尺寸常量
	static constexpr int ICON_W = 248;
	static constexpr int ICON_H = 260;
	static constexpr int ICON_SELECTED_W = 248;
	static constexpr int ICON_SELECTED_H = 260;

	// 每页显示图标数（滑动窗口）
	static constexpr int ICONS_PER_PAGE = 4;

	// 私有方法
	void update_selection();
	void create_status_bar();
	static void btn_next_cb(lv_event_t* e);
	static void btn_prev_cb(lv_event_t* e);
	static void btn_start_cb(lv_event_t* e);
	static void btnPowerCb(lv_event_t* e);
	static void btnBrightnessCb(lv_event_t* e);
	static void btnSettingsCb(lv_event_t* e);
	static void navCb(int action);

	// HTTP 导航回调：通过 VirtualIndev 注入触控到按钮坐标
	static DesktopApp* s_instance;
};
