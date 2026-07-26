#pragma once

#include <cstdint>
#include <functional>
#include "app/tetris/gameLogic/gameState.hpp"

/**
 * @brief Tetris TCP 游戏服务器 — P4-to-P4 专用通道
 *
 * 监听端口 8083，接受从机 TCP 连接。
 * JSON 协议与 /ws/tetris 完全一致 (join/input/snapshot)，
 * 但走纯 TCP 无 WS 帧头。
 *
 * 消息格式：newline 分隔的 JSON，每条完整 JSON + \n。
 */

/// TCP 主机回调（与 tetrisNet 的 hostCb 同构）
struct TetrisTcpHostCallbacks {
	std::function<void(int playerId, int tcpClientId)>  onJoin;
	std::function<void(int playerId, const char* key, bool down)> onInput;
};

/// 启动 TCP 游戏服务器（host 模式调用）
/// @param port  监听端口 (默认 8083)
/// @param cb    回调（同 tetrisNet hostCb）
bool tetrisTcpHostStart(uint16_t port, const TetrisTcpHostCallbacks& cb);

/// 停止 TCP 游戏服务器
void tetrisTcpHostStop();

/// 是否有已连接的 TCP 客户端
bool tetrisTcpHostHasClients();

/// 广播 snapshot 到所有 TCP 客户端
void tetrisTcpHostBroadcastSnapshot(int playerIndex, const GameState& state);
