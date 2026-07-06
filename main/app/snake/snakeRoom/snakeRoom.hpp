#pragma once

#include "app/app.hpp"
#include "bleGamepad/gamepadState.hpp"

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
class SnakeRoom final : public App
{
public:
	static constexpr char TAG[] = "SnakeRoom";

	SnakeRoom(Display* display);
	~SnakeRoom() override;

	void init() override;

	// BLE 手柄输入
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

	void onForeground() override;

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

	// 焦点导航
	int8_t m_focusIdx = 0;
	TickType_t m_nextMoveTime = 0;
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons = 0;  // 上一帧按钮状态（边沿检测）
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(150);
	static constexpr TickType_t ACTION_COOLDOWN = pdMS_TO_TICKS(500);

	void applyFocus();
	void activateFocus();

	/** @brief LVGL 按钮回调 */
	static void onBackBtnCb(lv_event_t* e);
	static void btn1PCb(lv_event_t* e);
	static void btn2PCb(lv_event_t* e);
	static void btn3PCb(lv_event_t* e);
	static void btn4PCb(lv_event_t* e);
};
