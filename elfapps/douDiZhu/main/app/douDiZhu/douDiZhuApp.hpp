#pragma once

#include "app/app.hpp"
#include "task/task.hpp"
#include "gamepad/gamepadState.hpp"
#include "douDiZhuLogic.hpp"
#include "douDiZhuTable.hpp"
#include "net/douDiZhuTcpHost.hpp"
#include "net/douDiZhuProtocol.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/**
 * @brief 斗地主主 App — 单机(1人+2AI) / 联机主机(Host, 权威服务器)
 *
 * 主机权威：m_game 持有全部手牌，只渲染本机(seat0)手牌；
 * 从机只收自己手牌 + 公开桌面（隐私隔离由 douDiZhuTcpHost 的 sendToSeat 保证）。
 *
 * 游戏循环：Task 轮询事件队列 + AI 时序，唯一修改 m_game 的上下文。
 */
class DouDizhuApp final : public App
{
public:
	static constexpr char TAG[] = "DouDizhuApp";

	enum class Mode : uint8_t { Single = 0, Host = 1 };

	DouDizhuApp(Display* display, Mode mode);
	~DouDizhuApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	Mode m_mode;

	DouDizhuGame m_game;
	DouDizhuTcpHost m_host;
	DouDizhuTable* m_table = nullptr;

	// ── 事件队列（本地输入 + 远程事件统一投递） ──
	struct DdzEvent {
		enum Type : uint8_t {
			RemoteBid, RemotePlay, RemotePass, RemoteRematch, RemoteDisconnect,
			LocalBid, LocalPlay, LocalPass, LocalHint, LocalRematch, LocalExit
		};
		Type type{};
		int  seat = 0;
		int  value = 0;
		uint8_t cards[DouDizhuLogic::MAX_HAND]{};
		int  count = 0;
	};
	QueueHandle_t m_eventQueue = nullptr;

	// ── 游戏 Task ──
	Task* m_gameTask = nullptr;
	static TickType_t gameTaskFn(void* param);
	void gameTick();
	void processEvents();

	// ── 状态 ──
	bool m_started = false;
	bool m_uiDirty = false;
	bool m_waitStatusShown = false;
	TickType_t m_lastConnectTick = 0;
	TickType_t m_aiDueTick = 0;
	int  m_bidCursor = 0;
	bool m_exiting = false;
	TickType_t m_turnStartTick = 0;
	int  m_countdownSec = 30;
	static constexpr int TURN_TIMEOUT_SEC = 30;

	// ── 输入去抖 ──
	TickType_t m_nextMoveTime[MaxHosts][MaxPlayers]{};
	TickType_t m_nextActionTime{};
	uint16_t m_prevButtons = 0;
	static constexpr TickType_t MOVE_DELAY_FIRST = pdMS_TO_TICKS(300);
	static constexpr TickType_t MOVE_DELAY = pdMS_TO_TICKS(120);
	static constexpr TickType_t ACTION_DELAY = pdMS_TO_TICKS(400);

	// ── 方法 ──
	void startRound(int dealer);
	void tryStart();
	bool isAISeat(int seat) const { return m_mode == Mode::Single && seat != 0; }

	void handleBid(int seat, int value);
	void handlePlay(int seat, const uint8_t* cards, int count);
	void handlePass(int seat);
	void handleRematch();
	void handleExit();
	void advanceAfterPlay(int seat);

	void doAIBid();
	void doAIPlay();
	void onHumanBid(int value);
	void onHumanPlay();
	void onHumanPass();
	void onHumanHint();

	void resetTurnTimer();
	void handleTimeout();
	void autoLead(int seat);

	void buildState(DouDizhuTable::State& st);
	void updateTable();

	// ── 联机发送（仅 Host 模式） ──
	void sendWelcome(int seat);
	void sendGameStart();
	void sendDeal(int seat);
	void sendBidTurn();
	void sendBidBroadcast(int seat, int value);
	void sendLandlord();
	void sendTurn();
	void sendPlayBroadcast(int seat, bool pass, bool newTrick);
	void sendHand(int seat);
	void sendGameOver();
	void sendAbort(const char* reason);

	// ── 输入处理 ──
	void onHumanInputBid(const GamepadState& state, uint16_t newPress);
	void onHumanInputPlay(const GamepadState& state, uint16_t newPress);
};
