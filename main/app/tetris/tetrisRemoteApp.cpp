#include "tetrisRemoteApp.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"
#include <esp_log.h>

static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// ============================================================
//  构造 / 析构
// ============================================================

TetrisRemoteApp::TetrisRemoteApp(Display* display,
                                 const char* remoteHost,
                                 uint16_t remotePort,
                                 int targetPlayer)
	: App(display)
	, m_remotePort(remotePort)
	, m_targetPlayer(targetPlayer)
{
	strncpy(m_remoteHost, remoteHost, sizeof(m_remoteHost) - 1);
}

TetrisRemoteApp::~TetrisRemoteApp()
{
	if (m_renderTask) {
		Task::removeTask(m_renderTask);
		m_renderTask = nullptr;
	}
}

// ============================================================
//  生命周期
// ============================================================

void TetrisRemoteApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard) {
		ESP_LOGE(TAG, "Failed to lock display");
		return;
	}

	lv_obj_set_style_bg_color(screen, LV_COLOR_MAKE(0x0D, 0x0D, 0x1A), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

	// ── Flex 行容器（与 TetrisApp 布局一致，让渲染器正确居中） ──
	auto flexRow = lv_obj_create(screen);
	lv_obj_remove_style_all(flexRow);
	lv_obj_set_size(flexRow, SCREEN_W, SCREEN_H);
	lv_obj_set_flex_flow(flexRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_scrollbar_mode(flexRow, LV_SCROLLBAR_MODE_OFF);

	// ── 渲染器（单个玩家，全屏宽） ──
	m_renderer = new TetrisRenderer(display, flexRow, SCREEN_W);
	m_renderer->bindGameState(&m_remoteState);

	// ── 连接状态标签（覆盖在 flexRow 上方） ──
	m_statusLabel = GUI::createLabel(screen, "正在连接...");
	lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_statusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(m_statusLabel, LV_ALIGN_TOP_MID, 0, 10);

	// ── 启动渲染 Task（30fps） ──
	m_renderTask = Task::addTask(renderTaskFn, "tetrisRemoteRender",
	                             this, 33, Task::Affinity::None);

	// ── 连接远程主机（TCP 同步连接，不阻塞） ──
	if (!m_client.connect(m_remoteHost, m_remotePort, m_targetPlayer)) {
		ESP_LOGE(TAG, "TCP connect failed");
		// 显示连接失败状态
		lv_label_set_text(m_statusLabel, "连接失败");
		lv_obj_set_style_text_color(m_statusLabel, GUI::Color::DANGER, 0);
	}

	// 连接成功后发 join（TCP 连接是同步的，连接成功 = 已就绪）
	vTaskDelay(pdMS_TO_TICKS(100));  // 等接收 Task 启动
	m_client.sendJoin();

	ESP_LOGI(TAG, "init done, connecting to %s:%d (target=%d)",
	         m_remoteHost, m_remotePort, m_targetPlayer);
}

void TetrisRemoteApp::deinit()
{
	m_client.disconnect();

	if (m_renderTask) {
		Task::removeTask(m_renderTask);
		m_renderTask = nullptr;
	}

	delete m_renderer;
	m_renderer = nullptr;

	App::deinit();
	ESP_LOGI(TAG, "deinit");
}

void TetrisRemoteApp::onForeground()
{
	m_prevButtons = 0xFFFF;
	m_lastLyUp = false;
	ESP_LOGI(TAG, "onForeground");
}

// ============================================================
//  渲染 Task
// ============================================================

TickType_t TetrisRemoteApp::renderTaskFn(void* param)
{
	auto* app = static_cast<TetrisRemoteApp*>(param);
	app->renderTick();
	return 33;  // ~30fps
}

void TetrisRemoteApp::renderTick()
{
	// ── 更新连接状态标签 ──
	if (auto guard = display->lockGuard()) {
		if (m_client.isConnected()) {
			lv_label_set_text(m_statusLabel, "已连接");
			lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUCCESS, 0);
		} else if (m_client.getPlayerId() < 0) {
			lv_label_set_text(m_statusLabel, "正在连接...");
			lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
		} else {
			lv_label_set_text(m_statusLabel, "连接断开");
			lv_obj_set_style_text_color(m_statusLabel, GUI::Color::DANGER, 0);
		}
		vTaskDelay(0);
	}

	// ── 同步游戏状态到渲染器 ──
	if (m_client.hasNewState()) {
		m_remoteState = m_client.getGameState();

		if (m_renderer) {
			m_renderer->bindGameState(&m_remoteState);
			m_renderer->syncState();
		}

		m_client.consumeNewState();
	}
}

// ============================================================
//  手柄输入 → 转发到远程主机
// ============================================================

void TetrisRemoteApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	// 只处理本机输入（hostId == 0 或 hostId == HostManager::getMySlot()）
	// 简单起见：只要 isConnected() 就转发所有输入
	if (!m_client.isConnected()) return;

	constexpr uint8_t deadZone = 13;
	constexpr uint8_t center = 128;

	bool lxLeft  = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp    = (state.ly < center - deadZone);
	bool lyDown  = (state.ly > center + deadZone);

	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	// B → 退出
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
		m_client.sendInput("exit", true);
		return;
	}

	// A → 逆时针旋转
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_A))
		m_client.sendInput("ccw", true);

	// B → 顺时针旋转（但上面已处理退出）
	// 只用 newPress 检测单独的 B 键按下做旋转，退出用长按
	// 实际上保留 BTN_B 的旋转功能：
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) &&
	    !(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3))) {
		// B 短按 = 顺时针旋转（非退出）
		m_client.sendInput("cw", true);
	}

	// X → Hold
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_X))
		m_client.sendInput("hold", true);

	// Y → 硬降
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_Y))
		m_client.sendInput("hard", true);

	// L3 → 退出
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)) {
		m_client.sendInput("exit", true);
		return;
	}

	// ── 摇杆方向（持续状态） ──
	m_client.sendInput("left",  lxLeft);
	m_client.sendInput("right", lxRight);
	m_client.sendInput("soft",  lyDown);

	// lyUp 边沿触发 → 切换攻击目标（由主机侧处理）
	if (lyUp && !m_lastLyUp) {
		m_client.sendInput("fw", true);  // "target up" 或 "attack switch"
	}
	m_lastLyUp = lyUp;
}
