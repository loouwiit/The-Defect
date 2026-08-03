#include "douDiZhuRemoteApp.hpp"
#include "display/font.hpp"
#include "host/hostManager.hpp"
#include "app/appStackManager.hpp"

#include <cstring>
#include <cstdio>
#include <esp_log.h>

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

DouDizhuRemoteApp::DouDizhuRemoteApp(Display* display)
	: App(display)
{
}

DouDizhuRemoteApp::~DouDizhuRemoteApp()
{
	deinit();
}

// ════════════════════════════════════════════════════════════════
//  生命周期
// ════════════════════════════════════════════════════════════════

void DouDizhuRemoteApp::init()
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

	m_table->onBid = [this](int value) { onHumanBid(value); };
	m_table->onPlay = [this]() { onHumanPlay(); };
	m_table->onPass = [this]() { onHumanPass(); };
	m_table->onHint = [this]() { onHumanHint(); };
	m_table->onRematch = [this]() { m_client.sendRematch(); };
	m_table->onExit = [this]() { requestExit(); };

	m_client.setOnDisconnected([this]() {
		m_table->setStatus("连接已断开（按 B 返回）");
	});

	// 启动渲染/轮询 Task
	m_gameTask = Task::addTask(gameTaskFn, "ddzRemoteTick", this, 50, Task::Affinity::None);

	// 本地监听 8086 等待主机连接（由主机发起 TCP）
	Task::addTask([](void* param) -> TickType_t {
		auto* app = static_cast<DouDizhuRemoteApp*>(param);
		if (!app->m_client.listen(DouDizhuMsg::CLIENT_PORT)) {
			ESP_LOGE(TAG, "TCP listen failed");
			app->m_table->setStatus("监听失败（按 B 返回）");
			return Task::infinityTime;
		}
		app->m_table->setStatus("已连接主机，等待开局...");
		return Task::infinityTime;
	}, "ddzListen", this, 0, Task::Affinity::None);
}

void DouDizhuRemoteApp::deinit()
{
	if (m_gameTask) {
		Task::removeTask(m_gameTask);
		m_gameTask = nullptr;
	}
	m_client.disconnect();
	if (m_table) {
		delete m_table;
		m_table = nullptr;
	}
	App::deinit();
}

void DouDizhuRemoteApp::onForeground()
{
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
	m_prevButtons = 0xFFFF;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
}

// ════════════════════════════════════════════════════════════════
//  渲染轮询
// ════════════════════════════════════════════════════════════════

TickType_t DouDizhuRemoteApp::gameTaskFn(void* param)
{
	static_cast<DouDizhuRemoteApp*>(param)->gameTick();
	return 50;
}

void DouDizhuRemoteApp::gameTick()
{
	if (!m_client.isConnected()) {
		if (!m_waitStatusShown) {
			m_waitStatusShown = true;
			if (m_client.getSeat() < 0)
				m_table->setStatus("等待主机连接...");
		}
		return;
	}

	if (m_client.hasNewState()) {
		m_view = m_client.getView();
		m_client.consumeNewState();
		m_wasMyPlayTurn = false;
		renderCurrent();
	}

	// ── 出牌倒计时 + 超时自动行动（本机回合） ──
	TickType_t now = xTaskGetTickCount();
	if (m_view.phase == 2 && m_view.turn == m_view.seat) {
		if (!m_wasMyPlayTurn) {
			m_wasMyPlayTurn = true;
			m_turnStartTick = now;
		}
		int elapsedSec = (int)((now - m_turnStartTick) / configTICK_RATE_HZ);
		int remain = TURN_TIMEOUT_SEC - elapsedSec;
		if (remain < 0) remain = 0;
		if (remain != m_countdownSec) {
			m_countdownSec = remain;
			renderCurrent();
		}
		if (remain <= 0) {
			if (m_view.myMustLead()) autoLead();
			else m_client.sendPass();
			m_wasMyPlayTurn = false;
			m_turnStartTick = now;
		}
	} else {
		m_wasMyPlayTurn = false;
	}
}

void DouDizhuRemoteApp::renderCurrent()
{
	if (auto guard = display->lockGuard()) {
		DouDizhuTable::State st;
		buildState(m_view, st);
		m_table->render(st);
	}
}

void DouDizhuRemoteApp::autoLead()
{
	uint8_t out[DouDizhuLogic::MAX_HAND];
	int n = DouDizhuLogic::findHint(m_view.hand, m_view.handCount,
	                                DouDizhuLogic::Combo{}, out, DouDizhuLogic::MAX_HAND);
	if (n == 0 && m_view.handCount > 0) { n = 1; out[0] = m_view.hand[0]; }
	if (n > 0) m_client.sendPlay(out, n);
}

void DouDizhuRemoteApp::buildState(const DouDizhuClientView& v, DouDizhuTable::State& st)
{
	st.mySeat = v.seat;
	st.phase = v.phase;
	st.handCount = v.handCount;
	memcpy(st.hand, v.hand, v.handCount * sizeof(uint8_t));

	st.bidTurn = v.bidTurn;
	st.minBid = v.minBid;
	st.maxBid = v.maxBid;
	st.maxBidder = v.maxBidder;
	st.dealer = v.dealer;

	st.turn = v.turn;
	st.landlord = v.landlord;
	st.bidValue = v.bidValue;

	for (int s = 0; s < 3; s++) {
		memcpy(st.trick[s], v.trickCards[s], v.trickCount[s] * sizeof(uint8_t));
		st.trickCount[s] = v.trickCount[s];
		st.trickPass[s] = v.trickPass[s];
		st.cardCount[s] = v.cardCount[s];
	}
	st.myMustLead = v.myMustLead();
	st.myCanPass = v.canPass();

	memcpy(st.bottom, v.bottom, sizeof(st.bottom));
	st.bottomRevealed = v.bottomRevealed;

	for (int s = 0; s < 3; s++) {
		st.scores[s] = v.scores[s];
		st.delta[s] = v.delta[s];
	}
	st.multiplier = v.multiplier;
	st.countdownSec = m_countdownSec;
	st.landlordWin = v.landlordWin;
	st.spring = v.spring;
	st.reverseSpring = v.reverseSpring;
	st.connected = m_client.isConnected();

	snprintf(st.names[0], sizeof(st.names[0]), "我");
	snprintf(st.names[1], sizeof(st.names[1]), "玩家2");
	snprintf(st.names[2], sizeof(st.names[2]), "玩家3");
}

DouDizhuLogic::Combo DouDizhuRemoteApp::currentPrev(const DouDizhuClientView& v) const
{
	// 本墩当前有效组合 = 有出牌的座位（trickCount>0）
	for (int s = 0; s < 3; s++) {
		if (v.trickCount[s] > 0) {
			return DouDizhuLogic::analyze(v.trickCards[s], v.trickCount[s]);
		}
	}
	return DouDizhuLogic::Combo{};
}

// ════════════════════════════════════════════════════════════════
//  本机动作
// ════════════════════════════════════════════════════════════════

void DouDizhuRemoteApp::onHumanBid(int value)
{
	DouDizhuClientView v = m_client.getView();
	if (v.phase != 1 || v.bidTurn != v.seat) return;
	if (value > 0 && value < v.minBid) return;
	m_client.sendBid(value);
}

void DouDizhuRemoteApp::onHumanPlay()
{
	DouDizhuClientView v = m_client.getView();
	if (v.phase != 2 || v.turn != v.seat) return;
	uint8_t sel[DouDizhuLogic::MAX_HAND];
	int n = m_table->getSelectedCards(sel, DouDizhuLogic::MAX_HAND);
	if (n == 0) { m_table->setStatus("请先选牌"); return; }

	DouDizhuLogic::Combo prev = currentPrev(v);
	if (v.myMustLead()) prev = DouDizhuLogic::Combo{};
	if (!DouDizhuLogic::isValidPlay(sel, n, prev)) {
		m_table->setStatus("出牌无效");
		return;
	}
	m_client.sendPlay(sel, n);
	m_table->clearSelection();
}

void DouDizhuRemoteApp::onHumanPass()
{
	DouDizhuClientView v = m_client.getView();
	if (v.phase != 2 || v.turn != v.seat) return;
	if (v.myMustLead()) { m_table->setStatus("必须出牌，不能不出"); return; }
	m_client.sendPass();
	m_table->clearSelection();
}

void DouDizhuRemoteApp::onHumanHint()
{
	DouDizhuClientView v = m_client.getView();
	if (v.phase != 2 || v.turn != v.seat) return;
	DouDizhuLogic::Combo prev = currentPrev(v);
	if (v.myMustLead()) prev = DouDizhuLogic::Combo{};
	uint8_t hint[DouDizhuLogic::MAX_HAND];
	int n = DouDizhuLogic::findHint(v.hand, v.handCount, prev, hint, DouDizhuLogic::MAX_HAND);
	if (n > 0) {
		m_table->selectCards(hint, n);
		m_table->setStatus("提示：已选好");
	} else {
		m_table->setStatus(v.myMustLead() ? "无牌可出" : "没有能压过的牌，可不出");
	}
}

void DouDizhuRemoteApp::requestExit()
{
	Task::addTask([](void* p) -> TickType_t {
		static_cast<DouDizhuRemoteApp*>(p)->popApp();
		return Task::infinityTime;
	}, "ddzRemoteExit", this, 0, Task::Affinity::None);
}

// ════════════════════════════════════════════════════════════════
//  手柄输入
// ════════════════════════════════════════════════════════════════

void DouDizhuRemoteApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	if (hostId != 0 || playerId != 0) return;
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;
	TickType_t now = xTaskGetTickCount();

	// 未连接时 B 返回
	if (!m_client.isConnected()) {
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) requestExit();
		return;
	}

	// 左摇杆 + dpad 合并方向（参考 ChineseChess 死区）
	constexpr uint8_t deadZone = 13, center = 128;
	bool left  = (state.lx < center - deadZone) || (state.dpad == 0 || state.dpad == 6);
	bool right = (state.lx > center + deadZone) || (state.dpad == 2 || state.dpad == 4);
	bool confirm = (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
	               (newPress & static_cast<uint16_t>(GamepadButton::BTN_L3));

	DouDizhuClientView v = m_client.getView();

	if (v.phase == 1 && v.bidTurn == v.seat) {
		int dx = left ? -1 : (right ? 1 : 0);
		if (dx != 0) {
			TickType_t delay = (m_nextMoveTime[0][0] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
			if (now >= m_nextMoveTime[0][0]) {
				m_nextMoveTime[0][0] = now + delay;
				m_bidCursor = (m_bidCursor + dx + 4) % 4;
			}
		}
		if (confirm && now >= m_nextActionTime) {
			m_nextActionTime = now + ACTION_DELAY;
			onHumanBid(m_bidCursor);
		}
		return;
	}

	if (v.phase == 2 && v.turn == v.seat) {
		int dx = left ? -1 : (right ? 1 : 0);
		if (dx != 0) {
			TickType_t delay = (m_nextMoveTime[0][0] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
			if (now >= m_nextMoveTime[0][0]) {
				m_nextMoveTime[0][0] = now + delay;
				m_table->moveCursor(dx);
			}
		}
		if (confirm) m_table->pressCurrent();
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) onHumanPass();
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_X)) onHumanPlay();
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_Y)) onHumanHint();
		return;
	}

	if (v.phase == 3) {
		if (confirm && now >= m_nextActionTime) { m_nextActionTime = now + ACTION_DELAY; m_client.sendRematch(); }
		if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) && now >= m_nextActionTime) { m_nextActionTime = now + ACTION_DELAY; requestExit(); }
		return;
	}
}
