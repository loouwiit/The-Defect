#pragma once

#include <cstdint>
#include <functional>
#include "wsServer/wsServer.hpp"
#include "app/tetris/gameLogic/gameState.hpp"

/* ============================================================
 *  Tetris 网络层
 *
 *  职责：
 *    - 通过 wsServer 注册 /ws/tetris 的 WebSocket handler
 *    - Host 模式：接收远程输入，发送 GameState snapshot
 *    - Client 模式：发送输入，接收 snapshot
 *
 *  消息类型（仅 2 种）：
 *    C→S: input   — 按键事件
 *    S→C: snapshot — 完整 GameState
 *
 *  使用方式：
 *    1. tetrisNetInit(true/false, callbacks) — 初始化
 *    2. 游戏循环每帧：Host 调 tetrisNetHostSendSnapshot()
 *                   Client 调 tetrisNetUpdate()
 *    3. tetrisNetDeinit() — 清理
 * ============================================================ */

// ============================================================
//  消息类型标签
// ============================================================

struct TetrisMsgTag {
    static constexpr const char* JOIN       = "join";
    static constexpr const char* JOIN_ACK   = "join_ack";
    static constexpr const char* INPUT      = "input";
    static constexpr const char* SNAPSHOT   = "snapshot";
};

// ============================================================
//  回调接口
// ============================================================

/// Host 端回调
struct TetrisHostCallbacks {
    std::function<void(int playerId, int fd)>          onJoin;   // 玩家加入
    std::function<void(int playerId, const char* key, bool down)> onInput; // 按键
};

/// Client 端回调
struct TetrisClientCallbacks {
    std::function<void(int playerId)>           onJoined;   // 连接成功
    std::function<void(const GameState& state)> onSnapshot; // 收到状态快照
};

// ============================================================
//  API
// ============================================================

/// 初始化
bool tetrisNetInit(bool isHost,
                   const TetrisHostCallbacks& hostCb = {},
                   const TetrisClientCallbacks& clientCb = {});

/// 反初始化
void tetrisNetDeinit();

/// 处理接收队列（Client 模式每帧调用）
void tetrisNetUpdate();

// ============================================================
//  Host 端 API
// ============================================================

/// 获取第一个已连接客户端的 fd，-1 表示无
int tetrisNetHostGetFirstClientFd();

/// 获取第一个客户端的目标玩家编号
int tetrisNetHostGetClientTarget();

/// 发送 GameState snapshot 给指定客户端
void tetrisNetHostSendSnapshot(int fd, int playerIndex, const GameState& state);

/// 广播 GameState snapshot 给所有客户端
void tetrisNetHostBroadcastSnapshot(int playerIndex, const GameState& state);

// ============================================================
//  Client 端 API
// ============================================================

bool tetrisNetClientConnect(const char* host, uint16_t port = 8080);
void tetrisNetClientDisconnect();
bool tetrisNetClientIsConnected();

/// 发送按键事件
void tetrisNetClientSendInput(const char* key, bool down);
