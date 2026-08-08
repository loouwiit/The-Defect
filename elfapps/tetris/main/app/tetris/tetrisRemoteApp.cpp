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

	// ── Flex 行容器（与 TetrisApp 布局一致） ──
	auto flexRow = lv_obj_create(screen);
	lv_obj_remove_style_all(flexRow);
	lv_obj_set_size(flexRow, SCREEN_W, SCREEN_H);
	lv_obj_set_flex_flow(flexRow, LV_FLEX_FLOW_ROW);
	lv_obj_set_scrollbar_mode(flexRow, LV_SCROLLBAR_MODE_OFF);

	// ── 渲染器（等宽分屏，最多 3 个玩家） ──
	// 初始创建 3 个，后续根据实际玩家数动态隐藏/显示
	int playerW = SCREEN_W / MAX_RENDER_PLAYERS;
	for (int i = 0; i < MAX_RENDER_PLAYERS; i++) {
		m_renderers[i] = new TetrisRenderer(display, flexRow, playerW);
		m_renderers[i]->bindGameState(&m_remoteStates[i]);
	}

	// ── 连接状态标签（覆盖在 flexRow 上方） ──
	m_statusLabel = GUI::createLabel(screen, "正在连接...");
	lv_obj_set_style_text_color(m_statusLabel, GUI::Color::SUBTLE, 0);
	lv_obj_set_style_text_font(m_statusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(m_statusLabel, LV_ALIGN_TOP_MID, 0, 10);

	// ── 启动渲染 Task（30fps） ──
	m_renderTask = Task::addTask(renderTaskFn, "tetrisRemoteRender",
	                             this, 33, Task::Affinity::None);

	// ── 断连回调：更新状态标签 ──
	m_client.setOnDisconnected([this]() {
		if (auto g = display->lockGuard()) {
			lv_label_set_text(m_statusLabel, "连接断开");
			lv_obj_set_style_text_color(m_statusLabel, GUI::Color::DANGER, 0);
		}
	});

	// ── 在本地监听，等待主机连接（由主机发起 TCP 连接） ──
	Task::addTask([](void* param) -> TickType_t {
		auto* app = static_cast<TetrisRemoteApp*>(param);
		if (!app->m_client.listen(8084)) {
			ESP_LOGE(TAG, "TCP listen failed");
			if (auto g = app->display->lockGuard()) {
				lv_label_set_text(app->m_statusLabel, "连接失败");
				lv_obj_set_style_text_color(app->m_statusLabel, GUI::Color::DANGER, 0);
			}
			return Task::infinityTime;
		}
		// 主机已连接，发送 join
		app->m_client.sendJoin();
		if (auto g = app->display->lockGuard()) {
			lv_label_set_text(app->m_statusLabel, "已连接");
			lv_obj_set_style_text_color(app->m_statusLabel, GUI::Color::SUCCESS, 0);
		}
		return Task::infinityTime;
	}, "tetrisListen", this, 0, Task::Affinity::None);

	ESP_LOGI(TAG, "init done, waiting for host connection on port 8084");
}

void TetrisRemoteApp::deinit()
{
	m_client.disconnect();

	if (m_renderTask) {
		Task::removeTask(m_renderTask);
		m_renderTask = nullptr;
	}

	for (int i = 0; i < MAX_RENDER_PLAYERS; i++) {
		delete m_renderers[i];
		m_renderers[i] = nullptr;
	}

	App::deinit();
	ESP_LOGI(TAG, "deinit");
}

void TetrisRemoteApp::onForeground()
{
	m_prevButtons = 0xFFFF;
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

	// ── 检测玩家数变化，动态调整渲染器 ──
	int count = m_client.getPlayerCount();
	if (count != m_renderCount && count > 0) {
		m_renderCount = count;
		int playerW = SCREEN_W / count;
		ESP_LOGI(TAG, "adjusting to %d players, width=%d", count, playerW);
		for (int i = 0; i < MAX_RENDER_PLAYERS; i++) {
			if (i < count) {
				lv_obj_clear_flag(m_renderers[i]->getContainer(), LV_OBJ_FLAG_HIDDEN);
			} else {
				lv_obj_add_flag(m_renderers[i]->getContainer(), LV_OBJ_FLAG_HIDDEN);
			}
		}
	}

	// ── 同步所有玩家状态到渲染器 ──
	for (int i = 0; i < m_renderCount; i++) {
		if (m_client.hasNewState(i)) {
			m_remoteStates[i] = m_client.getPlayerState(i);
			m_renderers[i]->bindGameState(&m_remoteStates[i]);
			m_renderers[i]->syncState();
			m_client.consumeNewState(i);
		}
	}
}

// ============================================================
//  手柄输入 → 转发到远程主机
// ============================================================

void TetrisRemoteApp::onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state)
{
	// 输入已由系统层 VirtualGamepad 通过 UDP 自动转发到主机，
	// 这里只处理本地 UI 操作。

	// 仅在连接断开时 B 可返回，防止误触退出游戏
	if (!m_client.isConnected()) {
		uint16_t newPress = state.buttons & ~m_prevButtons;
		m_prevButtons = state.buttons;

		if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B)) {
			Task::addTask([](void* p) -> TickType_t {
				static_cast<TetrisRemoteApp*>(p)->popApp();
				return Task::infinityTime;
			}, "remoteBack", this, 0, Task::Affinity::None);
		}
	}
}
