#pragma once

#include <cstdint>
#include <functional>
#include "app/tetris/gameLogic/gameState.hpp"
#include "mutex/mutex.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief Tetris 远程客户端 — 通过 TCP 连接远程 Tetris Host
 *
 * 使用裸 TCP socket 连接远程主机的 8083 端口。
 * JSON 协议 (newline 分隔) 与 /ws/tetris 兼容。
 *
 * 接收线程：后台 Task 持续 recv → 按 \n 分割 → 解析 snapshot。
 * 发送：直接在调用线程中 send。
 */
class TetrisRemoteClient {
public:
	TetrisRemoteClient();
	~TetrisRemoteClient();

	/// 在本地监听端口等待主机连接（反客为主：由主机发起 TCP）
	bool listen(int port = 8084);

	/// 断开连接
	void disconnect();

	/// 是否已连接
	bool isConnected() const { return m_sock >= 0; }

	/// 发送按键到远程主机
	void sendInput(const char* key, bool down);

	/// 获取指定玩家的最新 GameState（线程安全，返回拷贝）
	GameState getPlayerState(int player) const;

	/// 获取已发现的玩家数
	int getPlayerCount() const { return m_playerCount; }

	/// 是否有新状态
	bool hasNewState(int player) const;

	/// 消费新状态标志
	void consumeNewState(int player);

	/// 获取已分配的 player id
	int getPlayerId() const { return m_playerId; }

	/// 发送 join 消息
	void sendJoin();

	/// 设置断开回调
	void setOnDisconnected(std::function<void()> cb) {
		m_onDisconnected = std::move(cb);
	}

private:
	int  m_sock = -1;
	TaskHandle_t m_recvTask = nullptr;
	volatile bool m_running = false;

	static constexpr int MAX_REMOTE_PLAYERS = 3;

	mutable Mutex m_mutex;
	GameState m_states[MAX_REMOTE_PLAYERS]{};
	bool m_hasNewState[MAX_REMOTE_PLAYERS]{};
	int  m_playerCount = 0;

	int m_playerId = -1;
	int m_targetPlayer = 0;

	std::function<void()> m_onDisconnected;

	// 行缓冲区
	static constexpr size_t LINE_BUF_SIZE = 4096;
	char m_lineBuf[LINE_BUF_SIZE]{};
	size_t m_lineLen = 0;

	static void recvTaskFn(void* arg);
	void processRecvData(const char* data, size_t len);
	void processLine(const char* line);
};
