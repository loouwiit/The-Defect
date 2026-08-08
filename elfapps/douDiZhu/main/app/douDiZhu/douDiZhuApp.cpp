#include "douDiZhuApp.hpp"
#include "douDiZhuAI.hpp"
#include "display/font.hpp"
#include "host/hostManager.hpp"
#include "app/appStackManager.hpp"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_random.h>

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

DouDizhuApp::DouDizhuApp(Display* display, Mode mode)
	: App(display), m_mode(mode)
{
}

DouDizhuApp::~DouDizhuApp()
{
	deinit();
}

// ════════════════════════════════════════════════════════════════
//  生命周期
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard) {
		ESP_LOGE(TAG, "无法获取 LVGL 锁");
		return;
	}
	lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	m_table = new DouDizhuTable(display, screen);

	// 回调（LVGL 事件上下文 → 投递到事件队列）
	m_table->onBid = [this](int value) {
		DdzEvent e; e.type = DdzEvent::LocalBid; e.value = value;
		if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
	};
	m_table->onPlay = [this]() {
		DdzEvent e; e.type = DdzEvent::LocalPlay;
		if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
	};
	m_table->onPass = [this]() {
		DdzEvent e; e.type = DdzEvent::LocalPass;
		if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
	};
	m_table->onHint = [this]() {
		DdzEvent e; e.type = DdzEvent::LocalHint;
		if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
	};
	m_table->onRematch = [this]() {
		DdzEvent e; e.type = DdzEvent::LocalRematch;
		if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
	};
	m_table->onExit = [this]() {
		DdzEvent e; e.type = DdzEvent::LocalExit;
		if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
	};

	m_eventQueue = xQueueCreate(16, sizeof(DdzEvent));

	// ── Host 模式：启动 TCP 主机并连接从机 ──
	if (m_mode == Mode::Host) {
		DouDizhuTcpHost::Callbacks cb;
		cb.onBid = [this](int seat, int value) {
			DdzEvent e; e.type = DdzEvent::RemoteBid; e.seat = seat; e.value = value;
			if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
		};
		cb.onPlay = [this](int seat, const uint8_t* cards, int count) {
			DdzEvent e; e.type = DdzEvent::RemotePlay; e.seat = seat; e.count = count;
			memcpy(e.cards, cards, count * sizeof(uint8_t));
			if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
		};
		cb.onPass = [this](int seat) {
			DdzEvent e; e.type = DdzEvent::RemotePass; e.seat = seat;
			if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
		};
		cb.onRematch = [this](int seat) {
			DdzEvent e; e.type = DdzEvent::RemoteRematch; e.seat = seat;
			if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
		};
		cb.onDisconnect = [this](int seat) {
			DdzEvent e; e.type = DdzEvent::RemoteDisconnect; e.seat = seat;
			if (m_eventQueue) xQueueSend(m_eventQueue, &e, 0);
		};
		m_host.start(cb);
	}

	// 启动游戏 Task
	m_gameTask = Task::addTask(gameTaskFn, "douDizhuGame", this, 50, Task::Affinity::None);

	ESP_LOGI(TAG, "init done, mode=%d", (int)m_mode);
}

void DouDizhuApp::deinit()
{
	if (m_gameTask) {
		Task::removeTask(m_gameTask);
		m_gameTask = nullptr;
	}
	m_host.stop();
	if (m_eventQueue) {
		vQueueDelete(m_eventQueue);
		m_eventQueue = nullptr;
	}
	if (m_table) {
		delete m_table;
		m_table = nullptr;
	}
	App::deinit();
}

void DouDizhuApp::onForeground()
{
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
	m_prevButtons = 0xFFFF;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
}

// ════════════════════════════════════════════════════════════════
//  游戏 Task
// ════════════════════════════════════════════════════════════════

TickType_t DouDizhuApp::gameTaskFn(void* param)
{
	auto* self = static_cast<DouDizhuApp*>(param);
	self->gameTick();
	return 50;
}

void DouDizhuApp::gameTick()
{
	processEvents();

	if (!m_started) {
		tryStart();
		return;
	}

	// AI 时序 + 兜底：AI 回合未调度则补调度（防止任何原因导致的卡死）
	TickType_t now = xTaskGetTickCount();
	if (m_aiDueTick && now >= m_aiDueTick) {
		m_aiDueTick = 0;
		if (m_game.phase == DouDizhuGame::Phase::Bidding && isAISeat(m_game.bidTurn)) {
			doAIBid();
		} else if (m_game.phase == DouDizhuGame::Phase::Playing && isAISeat(m_game.turn)) {
			doAIPlay();
		}
	}
	if (m_aiDueTick == 0) {
		if (m_game.phase == DouDizhuGame::Phase::Bidding && isAISeat(m_game.bidTurn))
			m_aiDueTick = now + pdMS_TO_TICKS(900);
		else if (m_game.phase == DouDizhuGame::Phase::Playing && isAISeat(m_game.turn))
			m_aiDueTick = now + pdMS_TO_TICKS(900);
	}

	// ── 出牌倒计时 + 超时自动行动 ──
	if (m_game.phase == DouDizhuGame::Phase::Playing) {
		int elapsedSec = (int)((now - m_turnStartTick) / configTICK_RATE_HZ);
		int remain = TURN_TIMEOUT_SEC - elapsedSec;
		if (remain < 0) remain = 0;
		if (remain != m_countdownSec) { m_countdownSec = remain; m_uiDirty = true; }
		if (remain <= 0) handleTimeout();
	} else {
		if (m_countdownSec != TURN_TIMEOUT_SEC) m_countdownSec = TURN_TIMEOUT_SEC;
	}

	if (m_uiDirty) {
		m_uiDirty = false;
		updateTable();
	}
}

void DouDizhuApp::tryStart()
{
	if (m_mode == Mode::Single) {
		m_started = true;
		startRound(esp_random() % 3);
		return;
	}
	// Host：确保两从机连接，未连则重试
	bool c1 = m_host.isSeatConnected(1);
	bool c2 = m_host.isSeatConnected(2);
	if (c1 && c2) {
		m_started = true;
		startRound(esp_random() % 3);
		return;
	}
	// 节流：每 500ms 重试一次连接
	TickType_t now = xTaskGetTickCount();
	if (now < m_lastConnectTick) return;
	m_lastConnectTick = now + pdMS_TO_TICKS(500);

	const auto* slots = HostManager::instance().getSlots();
	if (!c1 && slots[1].connected && slots[1].ip[0]) {
		if (m_host.connectToClient(slots[1].ip, DouDizhuMsg::CLIENT_PORT, 1)) sendWelcome(1);
	}
	if (!c2 && slots[2].connected && slots[2].ip[0]) {
		if (m_host.connectToClient(slots[2].ip, DouDizhuMsg::CLIENT_PORT, 2)) sendWelcome(2);
	}
	if (!m_waitStatusShown) {
		m_waitStatusShown = true;
		m_table->setStatus("等待从机连接...");
	}
}

// ════════════════════════════════════════════════════════════════
//  事件处理
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::processEvents()
{
	DdzEvent e;
	while (m_eventQueue && xQueueReceive(m_eventQueue, &e, 0) == pdTRUE) {
		switch (e.type) {
		case DdzEvent::RemoteBid:    handleBid(e.seat, e.value); break;
		case DdzEvent::RemotePlay:   handlePlay(e.seat, e.cards, e.count); break;
		case DdzEvent::RemotePass:   handlePass(e.seat); break;
		case DdzEvent::RemoteRematch: handleRematch(); break;
		case DdzEvent::RemoteDisconnect:
			ESP_LOGW(TAG, "remote seat %d disconnected", e.seat);
			if (m_game.phase != DouDizhuGame::Phase::Settle) {
				handleExit();
			}
			break;
		case DdzEvent::LocalBid:     onHumanBid(e.value); break;
		case DdzEvent::LocalPlay:    onHumanPlay(); break;
		case DdzEvent::LocalPass:    onHumanPass(); break;
		case DdzEvent::LocalHint:    onHumanHint(); break;
		case DdzEvent::LocalRematch: handleRematch(); break;
		case DdzEvent::LocalExit:    handleExit(); break;
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  开局
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::startRound(int dealer)
{
	ESP_LOGI(TAG, "start round, dealer=%d", dealer);
	m_game.startGame(dealer);
	m_table->clearSelection();
	m_uiDirty = true;
	m_aiDueTick = 0;
	resetTurnTimer();

	if (m_mode == Mode::Host) {
		sendGameStart();
		sendDeal(1);
		sendDeal(2);
		sendBidTurn();
	}

	if (isAISeat(m_game.bidTurn))
		m_aiDueTick = xTaskGetTickCount() + pdMS_TO_TICKS(800);
}

// ════════════════════════════════════════════════════════════════
//  动作
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::handleBid(int seat, int value)
{
	if (m_game.phase != DouDizhuGame::Phase::Bidding) return;
	auto result = m_game.applyBid(seat, value);
	m_uiDirty = true;

	if (m_mode == Mode::Host) {
		sendBidBroadcast(seat, value);
		if (result == DouDizhuGame::BidResult::Landlord) {
			sendLandlord();
			if (m_game.landlord >= 1) sendHand(m_game.landlord);   // 地主拿底牌后手牌更新
			sendTurn();
		} else if (result == DouDizhuGame::BidResult::Redeal) {
			startRound((m_game.dealer + 1) % 3);   // 重发（会再次广播）
			return;
		} else {
			sendBidTurn();
		}
	}

	if (result == DouDizhuGame::BidResult::Continue) {
		if (isAISeat(m_game.bidTurn))
			m_aiDueTick = xTaskGetTickCount() + pdMS_TO_TICKS(800);
	} else if (result == DouDizhuGame::BidResult::Landlord) {
		// 定地主：清残留调度 + 重置出牌倒计时（地主先出）
		m_aiDueTick = 0;
		resetTurnTimer();
		ESP_LOGI(TAG, "landlord=seat%d turn=seat%d", m_game.landlord, m_game.turn);
		if (isAISeat(m_game.turn))
			m_aiDueTick = xTaskGetTickCount() + pdMS_TO_TICKS(900);
	}
}

void DouDizhuApp::handlePlay(int seat, const uint8_t* cards, int count)
{
	if (m_game.phase != DouDizhuGame::Phase::Playing) return;
	bool newTrick = m_game.mustLead(seat);
	if (!m_game.applyPlay(seat, cards, count)) {
		ESP_LOGW(TAG, "invalid play from seat %d", seat);
		return;
	}
	m_uiDirty = true;

	if (m_mode == Mode::Host) {
		if (seat >= 1) sendHand(seat);        // 隐私：只发给自己更新后的手牌
		sendPlayBroadcast(seat, false, newTrick);
		if (m_game.phase == DouDizhuGame::Phase::Settle) {
			sendGameOver();
			return;
		}
		sendTurn();
	}
	if (m_game.phase == DouDizhuGame::Phase::Settle) return;
	resetTurnTimer();
	if (isAISeat(m_game.turn))
		m_aiDueTick = xTaskGetTickCount() + pdMS_TO_TICKS(900);
}

void DouDizhuApp::handlePass(int seat)
{
	if (m_game.phase != DouDizhuGame::Phase::Playing) return;
	if (m_game.mustLead(seat)) return;
	m_game.applyPass(seat);
	m_uiDirty = true;

	if (m_mode == Mode::Host) {
		sendPlayBroadcast(seat, true, false);
		sendTurn();
	}
	resetTurnTimer();
	if (isAISeat(m_game.turn))
		m_aiDueTick = xTaskGetTickCount() + pdMS_TO_TICKS(900);
}

void DouDizhuApp::handleRematch()
{
	if (m_game.phase != DouDizhuGame::Phase::Settle) return;
	if (m_mode == Mode::Host && !(m_host.isSeatConnected(1) && m_host.isSeatConnected(2))) {
		ESP_LOGW(TAG, "clients missing, cannot rematch");
		return;
	}
	startRound((m_game.dealer + 1) % 3);
}

void DouDizhuApp::handleExit()
{
	if (m_exiting) return;
	m_exiting = true;
	if (m_mode == Mode::Host) {
		sendAbort("exit");
		m_host.stop();
	}
	Task::addTask([](void* p) -> TickType_t {
		static_cast<DouDizhuApp*>(p)->popApp();
		return Task::infinityTime;
	}, "ddzExit", this, 0, Task::Affinity::None);
}

// ════════════════════════════════════════════════════════════════
//  AI 动作（单机模式）
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::doAIBid()
{
	int seat = m_game.bidTurn;
	if (!isAISeat(seat)) return;
	int minBid = m_game.maxBid + 1;
	int bid = DouDizhuAI::chooseBid(m_game.hands[seat], m_game.handCount[seat], minBid, false);
	handleBid(seat, bid);
}

void DouDizhuApp::doAIPlay()
{
	int seat = m_game.turn;
	if (!isAISeat(seat)) return;
	// 新墩（先手）prev 重置为 None
	DouDizhuLogic::Combo prev = m_game.lastCombo;
	if (m_game.mustLead(seat)) prev = DouDizhuLogic::Combo{};
	uint8_t out[DouDizhuLogic::MAX_HAND];
	int n = DouDizhuAI::choosePlay(m_game.hands[seat], m_game.handCount[seat],
	                               prev, m_game.lastPlaySeat,
	                               m_game.mustLead(seat), seat, m_game.landlord,
	                               out, DouDizhuLogic::MAX_HAND);
	if (n > 0) {
		handlePlay(seat, out, n);
	} else {
		handlePass(seat);
	}
}

void DouDizhuApp::resetTurnTimer()
{
	m_turnStartTick = xTaskGetTickCount();
	m_countdownSec = TURN_TIMEOUT_SEC;
}

void DouDizhuApp::autoLead(int seat)
{
	uint8_t out[DouDizhuLogic::MAX_HAND];
	int n = DouDizhuLogic::findHint(m_game.hands[seat], m_game.handCount[seat],
	                                DouDizhuLogic::Combo{}, out, DouDizhuLogic::MAX_HAND);
	if (n == 0 && m_game.handCount[seat] > 0) { n = 1; out[0] = m_game.hands[seat][0]; }
	if (n > 0) handlePlay(seat, out, n);
}

void DouDizhuApp::handleTimeout()
{
	if (m_game.phase != DouDizhuGame::Phase::Playing) return;
	int seat = m_game.turn;
	int prevTurn = seat;
	if (seat == 0) {
		// 本机超时：必须出则自动出最小牌，否则自动不出
		if (m_game.mustLead(0)) autoLead(0);
		else handlePass(0);
	} else if (m_mode == Mode::Host) {
		// 远程从机超时，主机代行，避免联机卡死
		if (m_game.mustLead(seat)) autoLead(seat);
		else handlePass(seat);
	}
	// 行动若未推进回合（理论不发生），强制重置计时防死循环
	if (m_game.turn == prevTurn && m_game.phase == DouDizhuGame::Phase::Playing)
		resetTurnTimer();
}

// ════════════════════════════════════════════════════════════════
//  本机玩家动作（seat0）
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::onHumanBid(int value)
{
	if (m_game.phase != DouDizhuGame::Phase::Bidding || m_game.bidTurn != 0) return;
	if (value > 0 && value <= m_game.maxBid) return;   // 低于当前叫分无效
	handleBid(0, value);
}

void DouDizhuApp::onHumanPlay()
{
	if (m_game.phase != DouDizhuGame::Phase::Playing || m_game.turn != 0) return;
	uint8_t sel[DouDizhuLogic::MAX_HAND];
	int n = m_table->getSelectedCards(sel, DouDizhuLogic::MAX_HAND);
	if (n == 0) { m_table->setStatus("请先选牌"); return; }
	handlePlay(0, sel, n);
	m_table->clearSelection();
}

void DouDizhuApp::onHumanPass()
{
	if (m_game.phase != DouDizhuGame::Phase::Playing || m_game.turn != 0) return;
	if (m_game.mustLead(0)) { m_table->setStatus("必须出牌，不能不出"); return; }
	handlePass(0);
	m_table->clearSelection();
}

void DouDizhuApp::onHumanHint()
{
	if (m_game.phase != DouDizhuGame::Phase::Playing || m_game.turn != 0) return;
	DouDizhuLogic::Combo prev = m_game.lastCombo;
	if (m_game.mustLead(0)) prev = DouDizhuLogic::Combo{};
	uint8_t hint[DouDizhuLogic::MAX_HAND];
	int n = DouDizhuLogic::findHint(m_game.hands[0], m_game.handCount[0],
	                                prev, hint, DouDizhuLogic::MAX_HAND);
	if (n > 0) {
		m_table->selectCards(hint, n);
		m_table->setStatus("提示：已选好");
	} else {
		if (m_game.mustLead(0)) {
			m_table->setStatus("无牌可出");
		} else {
			m_table->setStatus("没有能压过的牌，可不出");
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  手柄输入
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	if (hostId != 0 || playerId != 0) return;   // 只响应本机 P0

	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;
	TickType_t now = xTaskGetTickCount();

	// 左摇杆 + dpad 合并方向（参考 ChineseChess 死区）
	constexpr uint8_t deadZone = 13, center = 128;
	bool left  = (state.lx < center - deadZone) || (state.dpad == 0 || state.dpad == 6);
	bool right = (state.lx > center + deadZone) || (state.dpad == 2 || state.dpad == 4);
	bool confirm = (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
	               (newPress & static_cast<uint16_t>(GamepadButton::BTN_L3));

	auto moved = [&]() -> bool {
		TickType_t delay = (m_nextMoveTime[0][0] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
		if (now < m_nextMoveTime[0][0]) return false;
		m_nextMoveTime[0][0] = now + delay;
		return true;
	};

	if (m_game.phase == DouDizhuGame::Phase::Bidding && m_game.bidTurn == 0) {
		if (left && moved()) m_bidCursor = (m_bidCursor + 3) % 4;
		else if (right && moved()) m_bidCursor = (m_bidCursor + 1) % 4;
		// 过滤不可用选项
		int tries = 0;
		while (m_bidCursor > 0 && m_bidCursor < m_game.maxBid + 1 && tries++ < 4)
			m_bidCursor = (m_bidCursor + 1) % 4;
		if (confirm && now >= m_nextActionTime) {
			m_nextActionTime = now + ACTION_DELAY;
			onHumanBid(m_bidCursor);
		}
		return;
	}

	if (m_game.phase == DouDizhuGame::Phase::Playing && m_game.turn == 0) {
		if ((left || right) && moved()) m_table->moveCursor(left ? -1 : 1);
		if (confirm) m_table->pressCurrent();
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) onHumanPass();
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_X)) onHumanPlay();
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_Y)) onHumanHint();
		return;
	}

	if (m_game.phase == DouDizhuGame::Phase::Settle) {
		if (confirm && now >= m_nextActionTime) { m_nextActionTime = now + ACTION_DELAY; handleRematch(); }
		if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) && now >= m_nextActionTime) { m_nextActionTime = now + ACTION_DELAY; handleExit(); }
		return;
	}
}

// ════════════════════════════════════════════════════════════════
//  渲染
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::buildState(DouDizhuTable::State& st)
{
	st.mySeat = 0;
	st.phase = (int)m_game.phase;
	st.handCount = m_game.handCount[0];
	memcpy(st.hand, m_game.hands[0], st.handCount * sizeof(uint8_t));

	st.bidTurn = m_game.bidTurn;
	st.minBid = m_game.maxBid + 1;
	st.maxBid = m_game.maxBid;
	st.maxBidder = m_game.maxBidder;
	st.dealer = m_game.dealer;

	st.turn = m_game.turn;
	st.landlord = m_game.landlord;
	st.bidValue = m_game.bidValue;

	for (int s = 0; s < 3; s++) {
		memcpy(st.trick[s], m_game.trickCards[s], m_game.trickCount[s] * sizeof(uint8_t));
		st.trickCount[s] = m_game.trickCount[s];
		st.trickPass[s] = m_game.trickPass[s];
		st.cardCount[s] = m_game.handCount[s];
	}
	st.myMustLead = m_game.mustLead(0);
	st.myCanPass = !m_game.mustLead(0);

	memcpy(st.bottom, m_game.bottom, sizeof(st.bottom));
	st.bottomRevealed = (m_game.landlord >= 0);

	for (int s = 0; s < 3; s++) {
		st.scores[s] = m_game.scores[s];
		st.delta[s] = m_game.delta[s];
	}
	st.multiplier = m_game.multiplier;
	st.countdownSec = m_countdownSec;
	st.landlordWin = m_game.landlordWin;
	st.spring = m_game.spring;
	st.reverseSpring = m_game.reverseSpring;

	snprintf(st.names[0], sizeof(st.names[0]), "我");
	if (m_mode == Mode::Host) {
		const auto* slots = HostManager::instance().getSlots();
		for (int s = 1; s <= 2; s++) {
			if (slots[s].connected) snprintf(st.names[s], sizeof(st.names[s]), "%.48s", slots[s].hostname);
			else snprintf(st.names[s], sizeof(st.names[s]), "玩家%d", s + 1);
		}
		st.connected = m_host.isSeatConnected(1) && m_host.isSeatConnected(2);
	} else {
		snprintf(st.names[1], sizeof(st.names[1]), "AI-1");
		snprintf(st.names[2], sizeof(st.names[2]), "AI-2");
	}
}

void DouDizhuApp::updateTable()
{
	if (!m_table) return;
	if (auto guard = display->lockGuard()) {
		DouDizhuTable::State st;
		buildState(st);
		m_table->render(st);
	}
}

// ════════════════════════════════════════════════════════════════
//  联机发送（Host）
// ════════════════════════════════════════════════════════════════

void DouDizhuApp::sendWelcome(int seat)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::WELCOME);
	cJSON_AddNumberToObject(root, "seat", seat);
	cJSON_AddNumberToObject(root, "player_count", 3);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.sendToSeat(seat, str); cJSON_free(str); }
}

void DouDizhuApp::sendGameStart()
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::GAME_START);
	cJSON_AddNumberToObject(root, "dealer", m_game.dealer);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendDeal(int seat)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::DEAL);
	cJSON* arr = cJSON_CreateArray();
	for (int i = 0; i < m_game.handCount[seat]; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(m_game.hands[seat][i]));
	cJSON_AddItemToObject(root, "cards", arr);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.sendToSeat(seat, str); cJSON_free(str); }
}

void DouDizhuApp::sendBidTurn()
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::BID_TURN);
	cJSON_AddNumberToObject(root, "seat", m_game.bidTurn);
	cJSON_AddNumberToObject(root, "min_bid", m_game.maxBid + 1);
	cJSON_AddNumberToObject(root, "max_bid", m_game.maxBid);
	cJSON_AddNumberToObject(root, "max_bidder", m_game.maxBidder);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendBidBroadcast(int seat, int value)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::BID);
	cJSON_AddNumberToObject(root, "seat", seat);
	cJSON_AddNumberToObject(root, "bid", value);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendLandlord()
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::LANDLORD);
	cJSON_AddNumberToObject(root, "seat", m_game.landlord);
	cJSON_AddNumberToObject(root, "bid", m_game.bidValue);
	cJSON* bt = cJSON_CreateArray();
	for (int i = 0; i < DouDizhuLogic::BOTTOM_SIZE; i++) cJSON_AddItemToArray(bt, cJSON_CreateNumber(m_game.bottom[i]));
	cJSON_AddItemToObject(root, "bottom", bt);
	cJSON* counts = cJSON_CreateArray();
	for (int i = 0; i < 3; i++) cJSON_AddItemToArray(counts, cJSON_CreateNumber(m_game.handCount[i]));
	cJSON_AddItemToObject(root, "counts", counts);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendTurn()
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::TURN);
	cJSON_AddNumberToObject(root, "seat", m_game.turn);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendPlayBroadcast(int seat, bool pass, bool newTrick)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::PLAY);
	cJSON_AddNumberToObject(root, "seat", seat);
	cJSON_AddBoolToObject(root, "pass", pass);
	cJSON_AddBoolToObject(root, "new_trick", newTrick);
	if (!pass) {
		cJSON* arr = cJSON_CreateArray();
		for (int i = 0; i < m_game.trickCount[seat]; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(m_game.trickCards[seat][i]));
		cJSON_AddItemToObject(root, "cards", arr);
	}
	cJSON* counts = cJSON_CreateArray();
	for (int i = 0; i < 3; i++) cJSON_AddItemToArray(counts, cJSON_CreateNumber(m_game.handCount[i]));
	cJSON_AddItemToObject(root, "counts", counts);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendHand(int seat)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::HAND);
	cJSON* arr = cJSON_CreateArray();
	for (int i = 0; i < m_game.handCount[seat]; i++) cJSON_AddItemToArray(arr, cJSON_CreateNumber(m_game.hands[seat][i]));
	cJSON_AddItemToObject(root, "cards", arr);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.sendToSeat(seat, str); cJSON_free(str); }
}

void DouDizhuApp::sendGameOver()
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::GAME_OVER);
	cJSON_AddBoolToObject(root, "landlord_win", m_game.landlordWin);
	cJSON_AddNumberToObject(root, "multiplier", m_game.multiplier);
	cJSON_AddBoolToObject(root, "spring", m_game.spring);
	cJSON_AddBoolToObject(root, "reverse_spring", m_game.reverseSpring);
	cJSON* sc = cJSON_CreateArray();
	for (int i = 0; i < 3; i++) cJSON_AddItemToArray(sc, cJSON_CreateNumber(m_game.scores[i]));
	cJSON_AddItemToObject(root, "scores", sc);
	cJSON* dl = cJSON_CreateArray();
	for (int i = 0; i < 3; i++) cJSON_AddItemToArray(dl, cJSON_CreateNumber(m_game.delta[i]));
	cJSON_AddItemToObject(root, "delta", dl);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}

void DouDizhuApp::sendAbort(const char* reason)
{
	cJSON* root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "type", DouDizhuMsg::ABORT);
	cJSON_AddStringToObject(root, "reason", reason);
	char* str = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	if (str) { m_host.broadcast(str); cJSON_free(str); }
}
