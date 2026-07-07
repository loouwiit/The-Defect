#pragma once

#include "app/app.hpp"
#include "bleGamepad/gamepadState.hpp"

/**
 * @brief 水果忍者游戏大厅
 *
 * 负责模式选择（Classic / Arcade）和人数选择（1P / 2P）。
 */
class FruitNinjaRoom final : public App
{
public:
	static constexpr char TAG[] = "FruitNinjaRoom";

	FruitNinjaRoom(Display* display);
	~FruitNinjaRoom() override;

	void init() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;
	void onForeground() override;

private:
	// ── UI ──
	lv_obj_t* m_backBtn{};
	lv_obj_t* m_title{};
	lv_obj_t* m_btnClassic{};
	lv_obj_t* m_btnArcade{};
	lv_obj_t* m_btn1P{};
	lv_obj_t* m_btn2P{};
	lv_obj_t* m_btnStart{};
	lv_obj_t* m_hintLabel{};

	// ── 状态 ──
	bool m_classicSelected = true;
	bool m_1PSelected = true;

	// ── 焦点导航 ──
	int8_t m_focusIdx = 0;
	TickType_t m_nextMoveTime[MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons = 0;
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(500);

	void createMenu(lv_obj_t* parent);
	void applyFocus();
	void activateFocus();
	void updateBtnStyles();

	static void onBackBtnCb(lv_event_t* e);
	static void onClassicCb(lv_event_t* e);
	static void onArcadeCb(lv_event_t* e);
	static void on1PCb(lv_event_t* e);
	static void on2PCb(lv_event_t* e);
	static void onStartCb(lv_event_t* e);
};
