#pragma once

#include "app/app.hpp"
#include "app/tetris/net/tetrisRemoteClient.hpp"
#include "app/tetris/renderer/tetris_renderer.hpp"
#include "task/task.hpp"

/**
 * @brief Tetris 远程游戏 App — 作为从机渲染远程主机的游戏画面
 *
 * 连接远程主机的 /ws/tetris，接收 snapshot 后在本地 LVGL 渲染。
 * 本地 BLE 手柄输入通过 sendInput() 转发到远程主机。
 */
class TetrisRemoteApp final : public App
{
public:
	static constexpr char TAG[] = "TetrisRemoteApp";

	/// @param remoteHost  远程主机 IP
	/// @param remotePort  远程主机 TCP 端口 (8083)
	/// @param targetPlayer 要观看/操作的玩家槽位 (0-2)
	TetrisRemoteApp(Display* display,
	                const char* remoteHost,
	                uint16_t remotePort = 8083,
	                int targetPlayer = 0);

	~TetrisRemoteApp() override;

	void init() override;
	void deinit() override;
	void onForeground() override;
	void onGamepadInput(uint8_t hostId, uint8_t playerId, const GamepadState& state) override;

private:
	static constexpr int MAX_RENDER_PLAYERS = 3;

	TetrisRemoteClient m_client;
	TetrisRenderer*    m_renderers[MAX_RENDER_PLAYERS]{};
	int                m_renderCount = 0;

	char m_remoteHost[64];
	uint16_t m_remotePort;
	int  m_targetPlayer;

	// 渲染器引用的 GameState（持久化，多玩家）
	GameState m_remoteStates[MAX_RENDER_PLAYERS]{};

	// ── 渲染 Task ──
	Task* m_renderTask = nullptr;
	static TickType_t renderTaskFn(void* param);
	void renderTick();

	// ── 连接状态 UI ──
	lv_obj_t* m_statusLabel{};

	// ── 输入去重 ──
	uint16_t m_prevButtons = 0;
	bool     m_lastLyUp = false;
};
