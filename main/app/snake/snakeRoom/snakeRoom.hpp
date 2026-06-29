#pragma once

#include "app/app.hpp"

/**
 * @brief 贪吃蛇游戏大厅
 *
 * 负责：
 *   - 显示游戏模式选择（1P / 2P / 3P / 4P）
 *   - 未来：WebSocket 玩家匹配、其他设备用户列表
 *
 * 选择人数后 push SnakeGame 到当前栈，
 * SnakeGame pop 后自动回到此大厅。
 */
class SnakeRoom : public App
{
public:
	static constexpr char TAG[] = "SnakeRoom";

	SnakeRoom(Display* display);
	~SnakeRoom() override;

	void init() override;

private:
	// 返回按钮
	lv_obj_t* m_backBtn{};

	// 人数选择按钮
	lv_obj_t* m_btn1P{};
	lv_obj_t* m_btn2P{};
	lv_obj_t* m_btn3P{};
	lv_obj_t* m_btn4P{};

	/** @brief 创建人数选择界面 */
	void createMenu(lv_obj_t* parent);

	/** @brief LVGL 按钮回调 */
	static void onBackBtnCb(lv_event_t* e);
	static void btn1PCb(lv_event_t* e);
	static void btn2PCb(lv_event_t* e);
	static void btn3PCb(lv_event_t* e);
	static void btn4PCb(lv_event_t* e);
};
