#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "gamepad/gamepadState.hpp"
#include "audio/Audio.hpp"
#include <cJSON.h>

/**
 * @brief 斗地主大厅
 *
 *   - 单机：1 人 vs 2 AI
 *   - 联机：3 台主机（1 主机 + 2 从机），复用 TetrisRoomApp 槽位卡片样式
 * 从机（isClient）进入后自动跳转 DouDizhuRemoteApp。
 */
class DouDizhuRoom final : public App
{
public:
	static constexpr char TAG[] = "DouDizhuRoom";

	DouDizhuRoom(Display* display);
	~DouDizhuRoom() override;

	/** @brief AI open_app 静态工厂 */
	static App* newApp(Display* display, const cJSON* params) {
		(void)params;
		return new DouDizhuRoom(display);
	}

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	// 返回按钮
	lv_obj_t* m_backBtn{};

	// 模式按钮
	lv_obj_t* m_btnSingle{};
	lv_obj_t* m_btnOnline{};

	// 槽位卡片（联机：slot 1/2）
	lv_obj_t* m_slotCards[2]{};
	lv_obj_t* m_slotNames[2]{};
	lv_obj_t* m_slotStatus[2]{};
	lv_obj_t* m_slotActivity[2]{};

	lv_obj_t* m_startBtn{};
	lv_obj_t* m_hintLbl{};

	// 焦点导航
	enum FocusGroup : uint8_t { FOCUS_BACK, FOCUS_MODE, FOCUS_BOTTOM };
	FocusGroup m_focusGroup = FOCUS_MODE;
	int8_t m_focusModeIdx = 0;     // 0=单机 1=联机
	int8_t m_focusBottomIdx = 0;   // 0=start
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons = 0;
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(500);

	lv_timer_t* m_refreshTimer{};

	bool m_starting = false;   // 防重复启动

	// ── 轮询 Task（统一执行延迟动作，避免一次性 Task 悬空指针） ──
	Task* m_roomTask = nullptr;
	enum class Pending : uint8_t { None, PushRemote, StartSingle, StartOnline, PopApp };
	Pending m_pending = Pending::None;
	static TickType_t roomTaskFn(void* param);
	void roomTick();

	void buildUi();
	void applyFocus();
	void activateFocus();
	void refreshSlots();
	void startGame();

	static void onBackBtnCb(lv_event_t* e);
	static void onModeBtnCb(lv_event_t* e);
	static void onStartBtnCb(lv_event_t* e);

	AudioHandle bgm{};

	// ── AI 能力 ──
	std::string getAiContext() override;
	void onAiSelect(const AiSelectTarget& target) override;
	void onAiActivate() override;
};
