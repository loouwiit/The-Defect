#pragma once

#include "app/app.hpp"
#include "bleGamepad/gamepadState.hpp"

/**
 * @brief 中国象棋游戏大厅
 *
 * 负责：
 *   - 显示游戏模式选择（人机对战 / 双人对战）
 *   - 选择模式后 push ChineseChessApp 到当前栈
 */
class ChineseChessRoom final : public App
{
public:
	static constexpr char TAG[] = "ChineseChessRoom";

	ChineseChessRoom(Display* display);
	~ChineseChessRoom() override;

	void init() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;
	void onForeground() override;

private:
	// 返回按钮
	lv_obj_t* m_backBtn{};

	// 模式选择按钮
	lv_obj_t* m_btnAI{};   // 人机对战
	lv_obj_t* m_btn2P{};   // 双人对战

	/** @brief 创建选择界面 */
	void createMenu(lv_obj_t* parent);

	// 焦点导航
	int8_t m_focusIdx = 0;
	TickType_t m_nextMoveTime = 0;
	uint16_t m_prevButtons = 0;
	TickType_t m_nextActionTime{};
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(150);
	static constexpr TickType_t ACTION_DELAY = 500;

	void applyFocus();
	void activateFocus();

	/** @brief LVGL 按钮回调 */
	static void onBackBtnCb(lv_event_t* e);
	static void btnAICb(lv_event_t* e);
	static void btn2PCb(lv_event_t* e);
};
