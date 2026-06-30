#pragma once

#include <cstdint>
#include <functional>
#include "wsServer/wsServer.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"

/* ============================================================
 *  Tetris 网络层
 *
 *  职责：
 *    - 通过 wsServer 注册 /ws/tetris 的 WebSocket handler
 *    - 提供 Host/Client 两套 API
 *    - 消息序列化/反序列化（cJSON 文本）
 *    - 连接管理 + 消息路由
 *
 *  使用方式：
 *    1. tetrisNetInit() — 初始化（注册 handler）
 *    2. 游戏循环每帧调用 tetrisNetUpdate()
 *    3. 锁块/消行/死亡时调用对应发送函数
 *    4. tetrisNetDeinit() — 清理
 * ============================================================ */

// ============================================================
//  消息类型
// ============================================================

/// 消息类型简短标记（对应 JSON 中 "t" 字段）
/// 消息类型全称（对应 JSON 中 "type" 字段值）
struct TetrisMsgTag {
    static constexpr const char* JOIN       = "join";
    static constexpr const char* JOIN_ACK   = "join_ack";
    static constexpr const char* PIECE      = "piece";
    static constexpr const char* LOCK       = "lock";
    static constexpr const char* ATTACK     = "attack";
    static constexpr const char* GARBAGE    = "garbage";
    static constexpr const char* BOARD_DELTA= "board_delta";
    static constexpr const char* BOARD_FULL = "board_full";
    static constexpr const char* MOVE       = "move";
    static constexpr const char* GAME_OVER  = "game_over";
    static constexpr const char* WINNER     = "winner";
    static constexpr const char* INPUT      = "input";
    static constexpr const char* PING       = "ping";
    static constexpr const char* PONG       = "pong";
};

// ============================================================
//  回调接口
// ============================================================

/// Host 端回调（TetrisApp 注册）
struct TetrisHostCallbacks {
    std::function<void(int playerId, int fd)>    onJoin;       // 玩家加入
    std::function<void(int playerId, const char* key, bool down)> onInput; // 按键
    std::function<void(int playerId, int lines)>   onAttack;    // 收到攻击
    std::function<void(int playerId)>            onGameOver;   // 玩家死亡
};

/// Client 端回调（TetrisApp 注册）
struct TetrisClientCallbacks {
    std::function<void(int playerId)>          onJoined;     // 连接成功
    std::function<void(PieceType type)>        onNewPiece;   // 收到新块
    std::function<void(int lines)>             onGarbage;    // 收到垃圾行
    std::function<void(int playerId)>          onGameOver;   // 远程玩家死亡
    std::function<void(int playerId)>          onWinner;     // 胜者宣布
};

// ============================================================
//  API
// ============================================================

/**
 * @brief 初始化 Tetris 网络层
 * @param isHost  true=Host 模式, false=Client 模式
 * @param hostCb  Host 回调（isHost=true 时使用）
 * @param clientCb Client 回调（isHost=false 时使用）
 * @return true 成功
 *
 * 内部通过 wsServerRegisterWs() 注册 /ws/tetris handler。
 */
bool tetrisNetInit(bool isHost,
                   const TetrisHostCallbacks& hostCb = {},
                   const TetrisClientCallbacks& clientCb = {});

/// @brief 反初始化（注销 handler + 断开连接）
void tetrisNetDeinit();

/// @brief 处理接收队列（游戏循环每帧调用）
void tetrisNetUpdate();

// ============================================================
//  Host 端 API
// ============================================================

/// 获取第一个已连接客户端的 fd，返回 -1 表示无客户端
int tetrisNetHostGetFirstClientFd();

/// 获取第一个已连接客户端的目标玩家编号，默认 0
int tetrisNetHostGetClientTarget();

/// 发送下一块给指定客户端
void tetrisNetHostSendPiece(int fd, PieceType type);

/// 广播下一块给所有客户端
void tetrisNetHostBroadcastPiece(PieceType type);

/// 发送垃圾行给指定客户端
void tetrisNetHostSendGarbage(int fd, int lines);

/// 发送棋盘快照给指定客户端
void tetrisNetHostSendBoardSnapshot(int fd, int playerIndex,
    const Board& board, const Piece& curPiece, int ghostY,
    int score, int level, int lines,
    const PieceType* nextPieces, int nextCount,
    PieceType holdPiece, int pendingGarbage);

/// 广播胜者
void tetrisNetHostBroadcastWinner(int playerId);

// ============================================================
//  Client 端 API
// ============================================================

/// 连接到远程 Host
bool tetrisNetClientConnect(const char* host, uint16_t port = 8080);

/// 断开连接
void tetrisNetClientDisconnect();

/// 是否已连接
bool tetrisNetClientIsConnected();

/// 通知 Host 锁块（触发下一块分发）
void tetrisNetClientSendLock();

/// 发送攻击（消行产生的垃圾行）
void tetrisNetClientSendAttack(int toId, int lines);

/// 通知 Host 游戏结束
void tetrisNetClientSendGameOver();

/// 发送方块位置（远程预览）
void tetrisNetClientSendMove(PieceType type, int x, int y, int r);
