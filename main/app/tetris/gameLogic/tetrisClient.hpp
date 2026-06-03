#pragma once

// ============================================================
//  TetrisClient — 单局游戏逻辑
//  不持有任何 UI 对象，纯数据逻辑
//  通过 GameEvent 回调与外部通信
// ============================================================

#include "tetrisTypes.hpp"
#include "board.hpp"
#include "pieceQueue.hpp"
#include "srs.hpp"

#include <functional>
#include <chrono>
#include <vector>

namespace tetris {

// 计分结果
struct ScoreResult {
    int lines = 0;        // 消行数
    int basePoints = 0;   // 基础分
    int tspinBonus = 0;   // T-Spin 奖励
    int b2bBonus = 0;     // Back-to-Back 奖励
    bool isTSpin = false;
    bool isB2B = false;
    int garbage = 0;      // 产生的攻击垃圾行数
};

// 难度等级（Guideline 速度曲线）
enum class Level : int { Level1 = 1, Level2, Level3, Level4, Level5,
                          Level6, Level7, Level8, Level9, Level10 };

// 时间配置
struct TimingConfig {
    int lockDelayMs = 500;       // Lock Delay 时间
    int dasMs = 170;             // Delayed Auto Shift
    int arrMs = 50;              // Auto Repeat Rate
    int softDropMs = 50;         // 软降间隔
    int gravityMs[10] = {        // 每级重力（毫秒/格）
        1000, 793, 618, 473, 355, 262, 190, 135, 94, 65
    };

    // 计算当前 level 的重力
    int gravity(int level) const {
        int idx = level - 1;
        if (idx < 0) idx = 0;
        if (idx > 9) idx = 9;
        return gravityMs[idx];
    }
};

class TetrisClient {
public:
    using EventCallback = std::function<void(const GameEvent&)>;

    TetrisClient();

    // 初始化一局新游戏
    void start(uint32_t rngSeed = 1);

    // 步进 (每帧调用，deltaMs 为自上次步进以来的时间)
    void tick(int deltaMs);

    // === 输入操作 ===
    bool moveLeft();
    bool moveRight();
    bool softDrop();         // 软降 1 格，返回是否成功移动
    int  hardDrop();         // 硬降到底，返回下落格数
    bool rotateCW();
    bool rotateCCW();
    bool hold();

    // === Hold 相关 ===
    bool canHold() const { return canHold_; }
    PieceType heldPiece() const { return held_; }

    // === 状态查询 ===
    const Board& board() const { return board_; }
    const PieceQueue& queue() const { return queue_; }
    PieceType currentType() const { return current_.type; }
    PieceType nextType() const { return queue_.peek(0); }
    Rotation currentRotation() const { return current_.rotation; }
    int currentX() const { return current_.x; }
    int currentY() const { return current_.y; }
    PieceSnapshot currentSnapshot() const;
    PieceSnapshot ghostSnapshot() const;

    // === 计分 ===
    int score() const { return score_; }
    int totalLines() const { return totalLines_; }
    int currentLevel() const;
    int b2bCount() const { return b2bCount_; }
    bool isGameOver() const { return gameOver_; }
    int pendingGarbage() const { return pendingGarbage_; }

    // === 网络/外部接口 ===
    void addGarbage(int lines, int holeColumn = -1);  // 接收攻击
    void setEventCallback(EventCallback cb) { callback_ = std::move(cb); }

    const TimingConfig& timing() const { return timing_; }
    TimingConfig& timing() { return timing_; }

    // 当前 piece 的 4×4 网格
    const MiniBoard& currentShape() const { return srs::getShape(current_.type, current_.rotation); }

    // 取 ghost Y（幽灵方块 Y 位置）
    int ghostY() const;

    // 消行后立即计算攻击分（用于网络发包）
    ScoreResult computeClearScore(int lines, bool isTSpin);

    // 清空 pending garbage（应用后调用）
    void consumeGarbage(int lines) {
        pendingGarbage_ = std::max(0, pendingGarbage_ - lines);
    }

private:
    // 内部状态
    void spawnPiece();
    void applyGravity(int count = 1);
    bool tryMove(int dx, int dy);
    bool tryRotate(Rotation newR);
    void lockActive();
    void emitEvent(GameEvent::Type type, int data = 0);

    Board board_;
    PieceQueue queue_;
    Piece current_{};      // 当前活动方块
    PieceType held_{ PieceType::None };
    bool canHold_{ true };

    // 计分
    int score_{ 0 };
    int totalLines_{ 0 };
    int b2bCount_{ 0 };
    bool gameOver_{ false };
    int pendingGarbage_{ 0 };  // 累积的等待注入垃圾行

    // 计时器
    TimingConfig timing_;
    int gravityAccumMs_{ 0 };   // 重力累积时间
    int lockTimerMs_{ 0 };      // Lock Delay 计时
    int dasLeftMs_{ 0 };        // DAS 方向 (-1 = 左, 0 = 无, 1 = 右)
    int dasRightMs_{ 0 };
    int arrAccumMs_{ 0 };       // ARR 累积

    EventCallback callback_;
};

}  // namespace tetris
