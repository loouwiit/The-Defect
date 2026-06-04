#pragma once

#include "tetris_client.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/**
 * @brief 单玩家完整游戏状态
 *
 * 包含 Board / PieceQueue / Scoring 以及所有游戏逻辑方法。
 * 不依赖 LVGL / Display，纯数据 + 算法。
 *
 * 用于多人模式：每个玩家持有一个 PlayerState 实例，
 * 各自独立运行游戏逻辑（spawn / move / rotate / lock / hold / 重力 / LockDelay）。
 *
 * 输入状态暴露为 public 字段，由外部（TetrisApp 或输入抽象层）写入。
 */
class PlayerState {
public:
    PlayerState() = default;

    // ============================================================
    //  初始化 / 重置
    // ============================================================

    void init();
    void reset();

    // ============================================================
    //  共享队列游标
    // ============================================================

    void setQueue(PieceQueue* q) { m_queue = q; }

    /// 取下一个方块（游标前进）
    PieceType nextPiece() { return m_queue->peek(m_pieceIndex++); }

    /// 预览未来第 slot 个方块
    PieceType peekPreview(int slot) const { return m_queue->peek(m_pieceIndex + slot); }

    // ============================================================
    //  游戏状态
    // ============================================================

    Board       board;
    Scoring     scoring;

    Piece       currentPiece;
    Piece       ghostPiece;
    PieceType   holdPiece   = PieceType::NONE;
    bool        holdUsed    = false;
    bool        gameOver    = false;

    // ============================================================
    //  计时器状态 (FreeRTOS tick, 1 tick ≈ 1ms)
    // ============================================================

    TickType_t  gravityTimer    = 0;
    TickType_t  lockTimer       = 0;
    TickType_t  dasTimer        = 0;
    TickType_t  arrTimer        = 0;
    int         gravityInterval = 1000;  // ms

    // ============================================================
    //  输入状态（外部写入）
    // ============================================================

    bool keyLeft   = false;
    bool keyRight  = false;
    bool keyCW     = false;
    bool keyCCW    = false;
    bool keySoft   = false;
    bool keyHard   = false;
    bool keyHold   = false;
    bool keyPause  = false;

    // ============================================================
    //  游戏逻辑方法
    // ============================================================

public:
    /// 生成下一个方块（从共享队列取）。返回 false = Game Over
    bool spawnPiece();

    /// 移动 (dx, dy)。返回 false = 碰撞
    bool movePiece(int dx, int dy);

    /// 顺时针旋转（含 Wall Kick）。返回 false = 无法旋转
    bool rotateCW();

    /// 逆时针旋转（含 Wall Kick）。返回 false = 无法旋转
    bool rotateCCW();

    /// 硬降到底 + 锁定
    void hardDrop();

    /// 锁定当前方块到棋盘（含消行计分）
    void lockPiece();

    /// Hold 交换
    void doHold();

    /// 注入垃圾行
    void addGarbage(int lines);

    /// 输入处理（DAS/ARR 左右移动 + 单次操作触发）
    void processInput();

    /// 游戏帧更新（重力下落 + Lock Delay）
    void updateGame();

    /// 根据消行数计算重力间隔
    int calcGravityInterval() const;

    /// 当前方块颜色值
    BoardCell currentColor() const { return pieceToColor(currentPiece.type()); }

    /// 上一次 lockPiece 产生的攻击行数（供外部读取后跨玩家应用）
    int  attackOut() const { return m_attackOut; }
    void clearAttackOut()  { m_attackOut = 0; }

private:
    PieceQueue* m_queue              = nullptr;
    int         m_pieceIndex         = 0;
    int         m_attackOut          = 0;
    bool        m_lastActionRotated  = false;   // 最后一次成功操作是否为旋转（T-Spin 判定用）

    /// 检测当前方块是否触底（下方有碰撞）
    bool isOnGround() const;
};
