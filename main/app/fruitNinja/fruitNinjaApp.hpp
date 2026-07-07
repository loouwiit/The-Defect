#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "fruitNinjaLogic.hpp"
#include "fruitNinjaRenderer.hpp"
#include "bleGamepad/gamepadState.hpp"

/**
 * @brief 水果忍者游戏 App
 *
 * 核心玩法：手柄方向(↑↓←→) + 按钮(A/B/X/Y) 匹配切水果。
 * 触屏可直接点按水果切片。
 *
 * 支持 Classic / Arcade 模式，1P / 2P 左右分屏。
 */
class FruitNinjaApp : public App
{
public:
	static constexpr char TAG[] = "FruitNinjaApp";

	FruitNinjaApp(Display* display,
		FruitNinjaLogic::GameMode mode,
		int playerCount);
	~FruitNinjaApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onBackground() override;
	void onGamepadInput(uint8_t playerId, const GamepadState& state) override;

private:
	// ── 核心 ──
	FruitNinjaLogic m_logic;
	FruitNinjaRenderer m_renderer;
	FruitNinjaLogic::GameMode m_mode;
	int m_playerCount;
	Thread m_thread;

	// ── 输入状态 ──
	uint16_t m_prevButtons[MaxPlayers]{};

	// ── 焦点 / 去重 ──
	TickType_t m_nextActionTime{};
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(500);

	// ── GameOver UI ──
	lv_obj_t* m_restartBtn{};
	lv_obj_t* m_backBtn{};
	int8_t m_focusIdx = 0;
	TickType_t m_nextMoveTime[MaxPlayers]{};
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);

	// ── 内部方法 ──
	static void gameLoop(void* param);
	static void onTouchCb(lv_event_t* e);

	static void btnRestartCb(lv_event_t* e);
	static void btnBackCb(lv_event_t* e);

	void applyFocus();
	void activateFocus();
};
