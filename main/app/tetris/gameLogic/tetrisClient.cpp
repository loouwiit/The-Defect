#include "tetrisClient.hpp"

#include <algorithm>
#include <cmath>

namespace tetris {

TetrisClient::TetrisClient() = default;

void TetrisClient::start(uint32_t rngSeed) {
    board_.clear();
    queue_.reset(rngSeed);
    held_ = PieceType::None;
    canHold_ = true;
    score_ = 0;
    totalLines_ = 0;
    b2bCount_ = 0;
    gameOver_ = false;
    pendingGarbage_ = 0;
    gravityAccumMs_ = 0;
    lockTimerMs_ = 0;
    dasLeftMs_ = 0;
    dasRightMs_ = 0;
    arrAccumMs_ = 0;

    spawnPiece();
    emitEvent(GameEvent::Type::PieceSpawn);
}

void TetrisClient::spawnPiece() {
    PieceType t = queue_.pop();
    current_.type = t;
    current_.rotation = Rotation::R0;
    // SRS spawn 位置: I 和 O 在中间，其他稍偏左
    if (t == PieceType::I) {
        current_.x = 3;
    } else if (t == PieceType::O) {
        current_.x = 4;
    } else {
        current_.x = 3;
    }
    current_.y = 0;

    canHold_ = true;
    lockTimerMs_ = 0;

    // 死亡检测：spawn 位置已冲突
    if (board_.collides(current_.x, current_.y, currentShape())) {
        gameOver_ = true;
        emitEvent(GameEvent::Type::GameOver);
    }
}

void TetrisClient::tick(int deltaMs) {
    if (gameOver_) return;

    // 处理重力
    gravityAccumMs_ += deltaMs;
    int g = timing_.gravity(currentLevel());
    while (gravityAccumMs_ >= g) {
        gravityAccumMs_ -= g;
        applyGravity();
        if (gameOver_) return;
    }

    // 处理 DAS/ARR
    if (dasLeftMs_ > 0 || dasRightMs_ > 0) {
        arrAccumMs_ += deltaMs;
        if (arrAccumMs_ >= timing_.arrMs) {
            arrAccumMs_ = 0;
            if (dasLeftMs_ > 0) tryMove(-1, 0);
            if (dasRightMs_ > 0) tryMove(1, 0);
        }
    }
}

bool TetrisClient::tryMove(int dx, int dy) {
    if (gameOver_) return false;
    if (board_.collides(current_.x + dx, current_.y + dy, currentShape())) {
        return false;
    }
    current_.x += dx;
    current_.y += dy;
    emitEvent(GameEvent::Type::PieceMove);
    return true;
}

void TetrisClient::applyGravity(int count) {
    for (int i = 0; i < count; ++i) {
        if (tryMove(0, 1)) {
            // 移动成功：重置 lock delay 计时 (除非卡住?)
            // 简化：始终重置
            lockTimerMs_ = 0;
        } else {
            // 不能下落：检查 lock delay
            lockTimerMs_ += timing_.gravity(currentLevel());
            if (lockTimerMs_ >= timing_.lockDelayMs) {
                lockActive();
            }
            break;
        }
    }
}

bool TetrisClient::moveLeft() {
    if (tryMove(-1, 0)) {
        // 移动成功，重置 lock delay
        lockTimerMs_ = 0;
        dasLeftMs_ = timing_.dasMs;
        return true;
    }
    return false;
}

bool TetrisClient::moveRight() {
    if (tryMove(1, 0)) {
        lockTimerMs_ = 0;
        dasRightMs_ = timing_.dasMs;
        return true;
    }
    return false;
}

bool TetrisClient::softDrop() {
    if (gameOver_) return false;
    if (tryMove(0, 1)) {
        score_ += 1;  // 软降奖励
        lockTimerMs_ = 0;
        return true;
    } else {
        // 落地了：触发 lock
        lockActive();
        return false;
    }
}

int TetrisClient::hardDrop() {
    if (gameOver_) return 0;
    int dropped = 0;
    while (tryMove(0, 1)) {
        ++dropped;
    }
    if (dropped > 0) {
        score_ += dropped * 2;  // 硬降奖励
        lockActive();
    } else {
        lockActive();
    }
    return dropped;
}

bool TetrisClient::tryRotate(Rotation newR) {
    if (gameOver_) return false;
    if (current_.type == PieceType::O) {
        current_.rotation = newR;
        emitEvent(GameEvent::Type::PieceRotate);
        return true;
    }
    if (newR == current_.rotation) return true;

    // 计算 kick 索引
    int kickIdx = srs::kickIndex(current_.rotation, newR);
    if (kickIdx < 0) return false;

    const srs::WallKickOffset* table;
    if (current_.type == PieceType::I) {
        table = srs::kI_Kicks[kickIdx];
    } else {
        table = srs::kJLSTZ_Kicks[kickIdx];
    }

    for (int i = 0; i < 4; ++i) {
        int nx = current_.x + table[i].dx;
        int ny = current_.y - table[i].dy;  // SRS y 朝上
        if (!board_.collides(nx, ny, srs::getShape(current_.type, newR))) {
            current_.x = nx;
            current_.y = ny;
            current_.rotation = newR;
            lockTimerMs_ = 0;
            emitEvent(GameEvent::Type::PieceRotate);
            return true;
        }
    }
    return false;  // 旋转失败
}

bool TetrisClient::rotateCW() {
    Rotation cur = current_.rotation;
    Rotation next = static_cast<Rotation>((static_cast<int>(cur) + 1) % 4);
    return tryRotate(next);
}

bool TetrisClient::rotateCCW() {
    Rotation cur = current_.rotation;
    Rotation next = static_cast<Rotation>((static_cast<int>(cur) + 3) % 4);
    return tryRotate(next);
}

bool TetrisClient::hold() {
    if (gameOver_ || !canHold_) return false;
    if (current_.type == PieceType::None) return false;

    PieceType prevHeld = held_;
    held_ = current_.type;
    canHold_ = false;

    if (prevHeld == PieceType::None) {
        // 取出下一个
        spawnPiece();
    } else {
        // 取出之前的
        current_.type = prevHeld;
        current_.rotation = Rotation::R0;
        current_.x = (prevHeld == PieceType::O) ? 4 : 3;
        current_.y = 0;
        if (board_.collides(current_.x, current_.y, currentShape())) {
            gameOver_ = true;
            emitEvent(GameEvent::Type::GameOver);
        }
    }

    lockTimerMs_ = 0;
    emitEvent(GameEvent::Type::HoldSwap);
    return true;
}

int TetrisClient::ghostY() const {
    int y = current_.y;
    const auto& shape = currentShape();
    while (!board_.collides(current_.x, y + 1, shape)) ++y;
    return y;
}

void TetrisClient::lockActive() {
    if (gameOver_) return;
    board_.lockPiece(current_.x, current_.y, currentShape(),
                     static_cast<Cell>(current_.type));

    // 消行
    int cleared = board_.clearLines();
    totalLines_ += cleared;
    emitEvent(GameEvent::Type::PieceLock);

    if (cleared > 0) {
        // 计分
        ScoreResult result = computeClearScore(cleared, false);
        score_ += result.basePoints * currentLevel() + result.tspinBonus + result.b2bBonus;
        if (result.isB2B) {
            emitEvent(GameEvent::Type::B2BTriggered);
        }
        emitEvent(GameEvent::Type::LineClear, cleared);

        // 应用 pending garbage
        if (pendingGarbage_ > 0) {
            // 标准 Tetris99 规则：自己消行 N 行，可抵消对方 N 行
            // 简化：1 行抵消 1 行
            int absorbed = std::min(pendingGarbage_, cleared);
            pendingGarbage_ -= absorbed;
            if (pendingGarbage_ > 0) {
                board_.addGarbage(pendingGarbage_);
                emitEvent(GameEvent::Type::AddGarbage, pendingGarbage_);
                pendingGarbage_ = 0;
            }
        }
    } else {
        // 没有消行：先应用 pending garbage
        if (pendingGarbage_ > 0) {
            board_.addGarbage(pendingGarbage_);
            emitEvent(GameEvent::Type::AddGarbage, pendingGarbage_);
            pendingGarbage_ = 0;
        }
    }

    // 死亡检测
    if (board_.isGameOver()) {
        gameOver_ = true;
        emitEvent(GameEvent::Type::GameOver);
        return;
    }

    // 生成下一个
    spawnPiece();
}

int TetrisClient::currentLevel() const {
    int lv = (totalLines_ / 10) + 1;
    if (lv < 1) lv = 1;
    if (lv > 20) lv = 20;
    return lv;
}

ScoreResult TetrisClient::computeClearScore(int lines, bool isTSpin) {
    ScoreResult r;
    r.lines = lines;
    r.isTSpin = isTSpin;

    if (isTSpin) {
        // T-Spin 评分
        switch (lines) {
            case 1: r.basePoints = 800; break;  // T-Spin Single
            case 2: r.basePoints = 1200; break; // T-Spin Double
            case 3: r.basePoints = 1600; break; // T-Spin Triple
            default: r.basePoints = 0;
        }
    } else {
        // 普通消行
        switch (lines) {
            case 1: r.basePoints = 100; break;
            case 2: r.basePoints = 300; break;
            case 3: r.basePoints = 500; break;
            case 4: r.basePoints = 800; break;  // Tetris
            default: r.basePoints = 0;
        }
    }

    // 攻击垃圾行
    if (isTSpin) {
        switch (lines) {
            case 0: r.garbage = 0; break;
            case 1: r.garbage = 2; break;
            case 2: r.garbage = 4; break;
            case 3: r.garbage = 6; break;
        }
    } else {
        switch (lines) {
            case 1: r.garbage = 0; break;
            case 2: r.garbage = 1; break;
            case 3: r.garbage = 2; break;
            case 4: r.garbage = 4; break;
        }
    }

    // Back-to-Back (Tetris 或 T-Spin)
    bool isDifficult = (lines == 4) || isTSpin;
    if (isDifficult && b2bCount_ > 0) {
        r.b2bBonus = r.basePoints / 2;
        r.isB2B = true;
    }
    if (isDifficult) ++b2bCount_;
    else b2bCount_ = 0;

    return r;
}

void TetrisClient::addGarbage(int lines, int holeColumn) {
    if (lines <= 0) return;
    pendingGarbage_ += lines;
    emitEvent(GameEvent::Type::AddGarbage, lines);
}

void TetrisClient::emitEvent(GameEvent::Type type, int data) {
    if (callback_) {
        GameEvent e;
        e.type = type;
        e.data = data;
        callback_(e);
    }
}

PieceSnapshot TetrisClient::currentSnapshot() const {
    return { current_.type, current_.rotation, current_.x, current_.y };
}

PieceSnapshot TetrisClient::ghostSnapshot() const {
    return { current_.type, current_.rotation, current_.x, ghostY() };
}

}  // namespace tetris
