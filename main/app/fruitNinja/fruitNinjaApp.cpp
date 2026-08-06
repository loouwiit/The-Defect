#include "fruitNinjaApp.hpp"
#include "task/task.hpp"
#include "esp_log.h"
#include "lvgl.h"

// ============================================================
// 构造 / 析构
// ============================================================

FruitNinjaApp::FruitNinjaApp(Display* display,
	FruitNinjaLogic::GameMode mode,
	int playerCount)
	: App(display)
	, m_mode(mode)
	, m_playerCount(playerCount)
{
}

FruitNinjaApp::~FruitNinjaApp() = default;

// ============================================================
// 初始化
// ============================================================

void FruitNinjaApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法锁定显示");
		return;
	}

	// 背景
	lv_obj_set_style_bg_color(screen, lv_color_hex(0xff0a0a1e), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// 创建渲染器对象池
	m_renderer.create(screen);

	// 配置游戏逻辑
	m_logic.setMode(m_mode);
	m_logic.setPlayerCount(m_playerCount);
	m_logic.reset();
	m_logic.setState(FruitNinjaLogic::State::Playing);

	// 注册退出按钮
	lv_obj_add_event_cb(m_renderer.getExitBtn(), btnExitCb, LV_EVENT_CLICKED, this);

	// 触屏输入：多指轮询 GT911Indev 槽位（gameLoop 内 updateTouchFromSlots 驱动）。
	// 不使用 LVGL indev 事件——多指下 LVGL 无法区分触摸 ID，会导致输入乱套。

	ESP_LOGI(TAG, "启动 FruitNinja: mode=%s, players=%d",
		(m_mode == FruitNinjaLogic::GameMode::Classic) ? "Classic" : "Arcade",
		m_playerCount);

	// 启动游戏循环
	running = true;
	m_thread = Thread{ gameLoop, "fruitNinja", this, Thread::Priority::Normal, 32768 };
}

// ============================================================
// 反初始化
// ============================================================

void FruitNinjaApp::deinit()
{
	running = false;
	vTaskDelay(pdMS_TO_TICKS(200));
	m_thread = {};

	// 隐藏 GameOver 对象（重启时使用 replaceWith，对象由 LVGL 自动清理）
	deletable = true;
}

// ============================================================
// 前台 / 后台
// ============================================================

void FruitNinjaApp::onForeground()
{
	for (auto& b : m_prevButtons) for (auto& i : b) i = 0xFFFF;
	for (auto& t : m_nextMoveTime) for (auto& i : t) i = 0;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	m_focusIdx = 0;
	for (auto& f : m_fingers)
		f = FingerTouch{};
	if (auto guard = display->lockGuard())
	{
		m_renderer.clearTrail();
		applyFocus();
	}
}

void FruitNinjaApp::onBackground()
{
}

// ============================================================
// 游戏循环
// ============================================================

void FruitNinjaApp::gameLoop(void* param)
{
	auto& self = *static_cast<FruitNinjaApp*>(param);
	ESP_LOGI(TAG, "游戏循环启动");

	constexpr float TICK_MS = 16.0f; // ~60fps
	TickType_t lastWake = xTaskGetTickCount();

	while (self.running)
	{
		float dt = TICK_MS / 1000.0f;

		// 游戏逻辑 + 帧前计算（纯计算，无 LVGL：全部在无锁区，缩短持锁窗口）
		self.m_logic.tick(dt);
		self.updateTouchFromSlots();
		self.m_renderer.ageTrail(TICK_MS / 1000.0f);
		self.m_renderer.beginFrame(self.m_logic);

		// 渲染提交（持锁：写 canvas buffer + invalidate + HUD）
		if (auto guard = self.display->lockGuard())
		{
			self.m_renderer.render(self.m_logic);

			// GameOver 检测
			if (self.m_logic.getState() == FruitNinjaLogic::State::GameOver && !self.m_restartBtn)
			{
				self.m_renderer.showGameOver(self.screen, self.m_logic,
					self.m_restartBtn, self.m_backBtn);

				if (self.m_restartBtn)
					lv_obj_add_event_cb(self.m_restartBtn, btnRestartCb, LV_EVENT_CLICKED, &self);
				if (self.m_backBtn)
					lv_obj_add_event_cb(self.m_backBtn, btnBackCb, LV_EVENT_CLICKED, &self);

				self.applyFocus();
			}
		}

		vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(static_cast<int>(TICK_MS)));
	}

	ESP_LOGI(TAG, "游戏循环结束");
	self.deletable = true;

	while (true)
		vTaskDelay(5000);
}

// ============================================================
// 触屏输入 — 多指轮询（替代 LVGL 事件，绕过 indev ID 不分问题）
//
// 每帧从 GT911Indev 读取 5 槽位快照，对按下手指做线段碰撞 + 产生刀光线段段。
// 触摸只负责“添加线段段 (上一帧位置, 当前帧位置)”；渲染层按线段池管理，
// 不关心手指 ID/槽位（GT911 ID 会复用）——段间天然断开、每段独立超时老化。
// ============================================================

void FruitNinjaApp::updateTouchFromSlots()
{
	// 非游戏状态：所有手指复位并清空刀光
	if (m_logic.getState() != FruitNinjaLogic::State::Playing)
	{
		for (auto& f : m_fingers)
			f = FingerTouch{};
		m_renderer.clearTrail();
		return;
	}

	GT911Indev::SlotInfo slots[GT911Indev::kSlotCount];
	GT911Indev::instance().getSlots(slots, GT911Indev::kSlotCount);

	for (int i = 0; i < GT911Indev::kSlotCount; i++)
	{
		const auto& info = slots[i];
		const bool pressed = info.active && info.pressed;
		const lv_point_t pt = info.point;
		auto& f = m_fingers[i];

		if (pressed)
		{
			if (f.hasPrev)
			{
				// ── 保持移动：线段碰撞 + 追加一段刀光线段 ──
				int dx = pt.x - f.prevPoint.x;
				int dy = pt.y - f.prevPoint.y;
				if (dx * dx + dy * dy >= 4)   // <2px 忽略（抖动抑制）
				{
					int player = 0;
					if (m_playerCount >= 2 && pt.x >= FruitNinjaLogic::SCREEN_W / 2)
						player = 1;

					m_logic.handleSwipe(player,
						static_cast<float>(f.prevPoint.x),
						static_cast<float>(f.prevPoint.y),
						static_cast<float>(pt.x),
						static_cast<float>(pt.y));

					m_renderer.addTrailSegment(
						static_cast<float>(f.prevPoint.x),
						static_cast<float>(f.prevPoint.y),
						static_cast<float>(pt.x),
						static_cast<float>(pt.y));
					f.prevPoint = pt;
				}
			}
			else
			{
				// ── 按压起点：仅记录上一帧位置（线段需两个点，首帧不产生段） ──
				f.prevPoint = pt;
				f.hasPrev = true;
			}
		}
		else
		{
			// ── 松手：停止追加，已有线段交由渲染器超时老化清理 ──
			f.hasPrev = false;
		}
	}
}

// ============================================================
// 手柄输入
// ============================================================

void FruitNinjaApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// 边沿检测
	uint16_t newPress = state.buttons & ~m_prevButtons[hostId][playerId];
	m_prevButtons[hostId][playerId] = state.buttons;

	// GameOver 时：A/L3 → activate, B → back, 摇杆→导航
	auto gameState = m_logic.getState();
	if (gameState == FruitNinjaLogic::State::GameOver)
	{
		// B 键返回
		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
		{
			if (xTaskGetTickCount() < m_nextActionTime) return;
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;

			Task::addTask([](void* p) -> TickType_t {
				static_cast<FruitNinjaApp*>(p)->popApp();
				return Task::infinityTime;
				}, "backToRoom", this, 0, Task::Affinity::None);
			return;
		}

		// A/L3 → 激活焦点
		if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
			(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
		{
			if (auto guard = display->lockGuard())
				activateFocus();
			return;
		}

		// 摇杆导航
		if (!lxLeft && !lxRight && !lyUp && !lyDown)
		{
			m_nextMoveTime[hostId][playerId] = 0;
			return;
		}
		if (m_nextMoveTime[hostId][playerId] >= xTaskGetTickCount()) return;

		TickType_t delay = (m_nextMoveTime[hostId][playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
		m_nextMoveTime[hostId][playerId] = xTaskGetTickCount() + delay;

		if (lyUp && m_focusIdx > 0) m_focusIdx--;
		if (lyDown && m_focusIdx < 1) m_focusIdx++;

		if (auto guard = display->lockGuard())
			applyFocus();
		return;
	}

	// ── 游戏中：手柄匹配 ──
	if (gameState != FruitNinjaLogic::State::Playing)
		return;

	// 确定方向
	FruitNinjaLogic::Direction dir;
	bool hasDir = false;
	if (lxLeft) { dir = FruitNinjaLogic::Direction::Left; hasDir = true; }
	else if (lxRight) { dir = FruitNinjaLogic::Direction::Right; hasDir = true; }
	else if (lyUp) { dir = FruitNinjaLogic::Direction::Up; hasDir = true; }
	else if (lyDown) { dir = FruitNinjaLogic::Direction::Down; hasDir = true; }

	if (!hasDir) return;

	// 确定按钮（仅边沿检测）
	FruitNinjaLogic::Button btn;
	bool hasBtn = false;
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) { btn = FruitNinjaLogic::Button::A; hasBtn = true; }
	else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) { btn = FruitNinjaLogic::Button::B; hasBtn = true; }
	else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_X)) { btn = FruitNinjaLogic::Button::X; hasBtn = true; }
	else if (newPress & static_cast<uint16_t>(GamepadButton::BTN_Y)) { btn = FruitNinjaLogic::Button::Y; hasBtn = true; }

	if (!hasBtn) return;

	m_logic.handleGamepadInput(playerId, dir, btn);
}

// ============================================================
// 焦点导航
// ============================================================

void FruitNinjaApp::applyFocus()
{
	auto clear = [](lv_obj_t* obj) { if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj) { if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clear(m_restartBtn);
	clear(m_backBtn);

	switch (m_focusIdx)
	{
	case 0: if (m_restartBtn) focus(m_restartBtn); break;
	case 1: if (m_backBtn)    focus(m_backBtn);    break;
	}
}

void FruitNinjaApp::activateFocus()
{
	switch (m_focusIdx)
	{
	case 0: if (m_restartBtn) lv_obj_send_event(m_restartBtn, LV_EVENT_CLICKED, nullptr); break;
	case 1: if (m_backBtn)    lv_obj_send_event(m_backBtn, LV_EVENT_CLICKED, nullptr);    break;
	}
}

// ============================================================
// 按钮回调
// ============================================================

void FruitNinjaApp::btnRestartCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤");
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_DELAY;

	Task::addTask([](void* p) -> TickType_t {
		auto* app = static_cast<FruitNinjaApp*>(p);
		app->replaceWith(new FruitNinjaApp(app->display, app->m_mode, app->m_playerCount));
		return Task::infinityTime;
		}, "restartFruitNinja", self, 0, Task::Affinity::None);
}

void FruitNinjaApp::btnExitCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤");
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_DELAY;

	Task::addTask([](void* p) -> TickType_t {
		static_cast<FruitNinjaApp*>(p)->popApp();
		return Task::infinityTime;
		}, "exitFruitNinja", self, 0, Task::Affinity::None);
}

void FruitNinjaApp::btnBackCb(lv_event_t* e)
{
	auto* self = static_cast<FruitNinjaApp*>(lv_event_get_user_data(e));

	if (xTaskGetTickCount() < self->m_nextActionTime)
	{
		ESP_LOGI(TAG, "多次点击，已过滤");
		return;
	}
	self->m_nextActionTime = xTaskGetTickCount() + self->ACTION_DELAY;

	Task::addTask([](void* p) -> TickType_t {
		static_cast<FruitNinjaApp*>(p)->popApp();
		return Task::infinityTime;
		}, "backToRoom", self, 0, Task::Affinity::None);
}
