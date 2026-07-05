#pragma once

#include "tetris_client.hpp"

/**
 * @brief 游戏状态 — 纯数据，贯穿三层（GameLogic → Renderer → Network）
 *
 * 这是一个扁平的、可拷贝的、可序列化的数据结构。
 * 不包含任何方法，只做数据承载。
 *
 * GameLogic 层写入，Renderer 和 Network 层读取。
 */
struct GameState {
    // ── 棋盘 ──
    Board board;

    // ── 当前方块 ──
    PieceType currentPieceType  = PieceType::NONE;
    int       currentPieceX     = 0;
    int       currentPieceY     = 0;
    Rotation  currentPieceRotation = Rotation::R0;

    // ── Ghost ──
    int ghostPieceX = 0;
    int ghostPieceY = 0;

    // ── 预览队列（Host 从共享 PieceQueue 填充） ──
    PieceType nextPieces[4] = {};

    // ── Hold ──
    PieceType holdPiece  = PieceType::NONE;
    bool      holdUsed   = false;

    // ── 计分 ──
    int score      = 0;
    int level      = 0;
    int totalLines = 0;
    int combo      = -1;  // -1 = 无 combo

    // ── 对战 ──
    int pendingGarbage = 0;
    int garbageFlash   = 0;

    // ── 状态 ──
    bool gameOver = false;
    bool active   = true;  // 玩家是否存活（淘汰后为 false）
};
