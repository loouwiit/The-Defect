#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "fruitNinjaLogic.hpp"
#include "fruitNinjaRenderer.hpp"
#include "gamepad/gamepadState.hpp"
#include "touch/gt911Indev.hpp"

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
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	// ── 核心 ──
	FruitNinjaLogic m_logic;
	FruitNinjaRenderer m_renderer;
	FruitNinjaLogic::GameMode m_mode;
	int m_playerCount;
	Thread m_thread;

	// ── 触摸跟踪（多指，轮询 GT911Indev 槽位）──
	// 槽位仅用于几何（上一帧坐标 → 本帧坐标生成线段 + 线段碰撞），
	// 不参与 UI 归属：渲染层按“线段段”池管理刀光，GT911 ID 会复用不可作 UI 依据。
	struct FingerTouch
	{
		lv_point_t prevPoint{};    // 上一帧坐标（线段碰撞/生成刀光段用）
		bool hasPrev = false;      // 是否已有上一帧坐标
	};
	FingerTouch m_fingers[GT911Indev::kSlotCount]{};

	// ── 输入状态 ──
	uint16_t m_prevButtons[MaxHosts][MaxPlayers]{};

	// ── 焦点 / 去重 ──
	TickType_t m_nextActionTime{};
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(500);

	// ── GameOver UI ──
	lv_obj_t* m_restartBtn{};
	lv_obj_t* m_backBtn{};
	int8_t m_focusIdx = 0;
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);

	// ── 内部方法 ──
	static void gameLoop(void* param);
	void updateTouchFromSlots();

	static void btnRestartCb(lv_event_t* e);
	static void btnExitCb(lv_event_t* e);
	static void btnBackCb(lv_event_t* e);

	void applyFocus();
	void activateFocus();
};
