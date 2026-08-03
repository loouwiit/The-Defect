#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "gamepad/gamepadState.hpp"
#include "douDiZhuTable.hpp"
#include "net/douDiZhuRemoteClient.hpp"

/**
 * @brief 斗地主从机 App — 渲染远程主机（只显示本机手牌 + 公开桌面）
 *
 * 本地监听 8086 等待主机连接；只收到自己手牌，对手仅牌背 + 出牌。
 */
class DouDizhuRemoteApp final : public App
{
public:
	static constexpr char TAG[] = "DouDizhuRemoteApp";

	DouDizhuRemoteApp(Display* display);
	~DouDizhuRemoteApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	DouDizhuRemoteClient m_client;
	DouDizhuTable* m_table = nullptr;
	DouDizhuClientView m_view;

	Task* m_gameTask = nullptr;
	static TickType_t gameTaskFn(void* param);
	void gameTick();

	bool m_waitStatusShown = false;
	TickType_t m_turnStartTick = 0;   // 回合同步基准（最近一次主机广播时刻），用于倒计时推算
	int  m_countdownSec = 30;
	bool m_wasMyPlayTurn = false;
	static constexpr int TURN_TIMEOUT_SEC = 30;

	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons = 0;
	int  m_bidCursor = 0;
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(400);

	void buildState(const DouDizhuClientView& v, DouDizhuTable::State& st);
	DouDizhuLogic::Combo currentPrev(const DouDizhuClientView& v) const;

	void onHumanBid(int value);
	void onHumanPlay();
	void onHumanPass();
	void onHumanHint();
	void requestExit();
	void renderCurrent();
	void autoLead();
};
