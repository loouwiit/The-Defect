#pragma once

#include "app/app.hpp"
#include "app/tetris/net/tetrisRemoteClient.hpp"
#include "app/tetris/renderer/tetris_renderer.hpp"
#include "audio/Audio.hpp"
#include "task/task.hpp"
#include <utility>

/**
 * @brief Tetris 远程游戏 App — 作为从机渲染远程主机的游戏画面
 *
 * 连接远程主机，接收 snapshot 后在本地 LVGL 渲染。
 * 本地 BLE 手柄输入由系统层 VirtualGamepad 通过 UDP 自动转发。
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

	/// 接管房间的 BGM（移动语义）：room 被 replaceWith 删除时其 AudioHandle 析构会停播，
	/// 先把循环 BGM 移交给远程游戏 App 继续播放（从机远程模式共用房间 BGM）。
	void takeBgm(AudioHandle&& bgm) { m_bgm = std::move(bgm); }

private:
	static constexpr int MAX_RENDER_PLAYERS = 3;

	TetrisRemoteClient m_client;
	TetrisRenderer*    m_renderers[MAX_RENDER_PLAYERS]{};
	int                m_renderCount = 0;

	char m_remoteHost[64];
	uint16_t m_remotePort;
	int  m_targetPlayer;

	// 从房间接管过来的 BGM（room 删除时由 takeBgm 移动转移，播放不中断）
	AudioHandle m_bgm{};

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
};
