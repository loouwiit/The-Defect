#pragma once

#include "tetris_client.hpp"
#include "gameState.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

/// Dirty flags 指示哪些视觉状态发生了变化
typedef uint8_t DirtyFlags;
constexpr DirtyFlags DIRTY_NONE    = 0;
constexpr DirtyFlags DIRTY_PIECE   = 1 << 0;  ///< 方块移动/旋转
constexpr DirtyFlags DIRTY_GHOST   = 1 << 1;  ///< ghost 位置变化
constexpr DirtyFlags DIRTY_BOARD   = 1 << 2;  ///< 棋盘内容变化
constexpr DirtyFlags DIRTY_HOLD    = 1 << 4;  ///< Hold 槽变化
constexpr DirtyFlags DIRTY_SCORE   = 1 << 5;  ///< 分数/combo 变化

/**
 * @brief 单玩家游戏状态 + 逻辑
 *
 * 在 Host-authoritative 模式下，所有 PlayerState 只运行在 Host 上。
 * 包含 Board / Scoring 以及所有游戏逻辑方法。
 *
 * Host 游戏循环每帧调用 processInput() + updateGame()，
 * lockPiece() 内部通过 nextPiece() 从共享队列取下一块。
 *
 * 输出方式：exportState() 填充 GameState，供 Renderer 和 Network 使用。
 */
class PlayerState {
public:
    PlayerState() = default;

    // ============================================================
    //  初始化 / 重置
    // ============================================================

    /// 初始化（从共享队列取首个方块）
    void init();
    void reset();

    // ============================================================
    //  游戏状态（public，供 Renderer/Network 间接读取）
    // ============================================================

    Board       board;
    Scoring     scoring;

    Piece       currentPiece;
    Piece       ghostPiece;
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
    //  输入状态（外部写入：触屏 / C6 / 网络输入）
    // ============================================================

    bool keyLeft   = false;
    bool keyRight  = false;
    bool keyCW     = false;
    bool keyCCW    = false;
    bool keySoft   = false;
    bool keyHard   = false;
    bool keyHold   = false;

    // 网络输入请求计数器（解决 WS 延迟吞操作）
    int  netLeftReq  = 0;
    int  netRightReq = 0;

    // ============================================================
    //  共享队列 + 游标
    // ============================================================

    void setQueue(PieceQueue* q) { m_queue = q; }

    /// 取下一个方块（从共享队列按游标取）
    PieceType nextPiece() {
        if (m_holdSlot != PieceType::NONE) {
            PieceType t = m_holdSlot;
            m_holdSlot = PieceType::NONE;
            return t;
        }
        return m_queue->peek(m_pieceIndex++);
    }

    /// 从队列 spawn 下一块（锁块 / init 时调用）
    void spawnPiece() { spawnType(nextPiece()); }

    /// 预览未来第 slot 个方块（基于当前游标，0=下一个）
    /// 有 holdSlot 时 slot 0 返回被 hold 的块
    PieceType peekPreview(int slot) const {
        if (m_holdSlot != PieceType::NONE) {
            if (slot == 0) return m_holdSlot;
            return m_queue->peek(m_pieceIndex + slot - 1);
        }
        return m_queue->peek(m_pieceIndex + slot);
    }

    // ============================================================
    //  游戏逻辑方法
    // ============================================================

public:
    /// 将当前状态导出到 GameState（供 Renderer / Network 使用）
    void exportState(GameState& out) const;

    /// 移动 (dx, dy)。返回 false = 碰撞
    bool movePiece(int dx, int dy);

    /// 顺时针旋转（含 Wall Kick）。返回 false = 无法旋转
    bool rotateCW();

    /// 逆时针旋转（含 Wall Kick）。返回 false = 无法旋转
    bool rotateCCW();

    /// 硬降到底 + 锁定
    void hardDrop();

    /// 锁定当前方块到棋盘（含消行计分，不 spawn 新块）
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

    /// 上一次 lockPiece 产生的攻击行数
    int  attackOut() const { return m_attackOut; }
    void clearAttackOut()  { m_attackOut = 0; }

    /// 待处理垃圾行
    int  pendingGarbage() const { return m_pendingGarbage; }

    /// Hold 槽中的方块类型
    PieceType holdPiece() const { return m_holdSlot; }

    /// 垃圾行闪烁提示
    int  garbageFlash() const { return m_garbageFlash; }

    /// 消费 dirty flags
    DirtyFlags consumeDirty() {
        DirtyFlags f = m_dirtyFlags;
        m_dirtyFlags = DIRTY_NONE;
        return f;
    }

    /// 强制设置 dirty flags
    void markDirty(DirtyFlags mask) { m_dirtyFlags |= mask; }

private:
    int         m_pieceIndex         = 0;
    PieceQueue* m_queue              = nullptr;
    int         m_attackOut          = 0;
    int         m_pendingGarbage     = 0;
    int         m_garbageFlash       = 0;
    PieceType   m_holdSlot           = PieceType::NONE;
    bool        m_lastActionRotated  = false;
    DirtyFlags  m_dirtyFlags         = DIRTY_NONE;

    void setDirty(DirtyFlags mask) { m_dirtyFlags |= mask; }

    /// 用指定类型生成当前方块（doHold 交换时用，不走队列）
    void spawnType(PieceType type);

    /// 检测当前方块是否触底
    bool isOnGround() const;
};
