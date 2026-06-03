#pragma once

// ============================================================
//  PieceQueue — 7-bag 随机方块生成器
//  Guideline 标准：每 7 块包含 1 次所有 7 种方块
// ============================================================

#include "tetrisTypes.hpp"
#include <array>
#include <cstdint>

namespace tetris {

// 简单的 LCG (Numerical Recipes constants) — 避免依赖全局 rand()
class LCG {
public:
    explicit LCG(uint32_t seed = 1) : state_(seed ? seed : 1) {}
    uint32_t next() {
        state_ = state_ * 1664525u + 1013904223u;
        return state_;
    }
    uint32_t nextInRange(uint32_t max) {
        return next() % max;
    }
private:
    uint32_t state_;
};

class PieceQueue {
public:
    static constexpr int kBagSize = 7;
    static constexpr int kNextCount = 5;  // 预览方块数量

    PieceQueue() = default;
    explicit PieceQueue(uint32_t seed) : rng_(seed) {}

    // 重置 RNG（用于同步多端使用相同 seed）
    void reset(uint32_t seed);

    // 取出下一个方块（消费 1 块，bag 空时重新洗牌）
    PieceType pop();

    // 预览接下来 N 个方块（不消费）
    PieceType peek(int index) const;

    // 当前已生成但未消费的方块队列（实际已发到玩家的）
    // 用于检查"接下来 N 个"是什么
    int queuedCount() const { return queuedCount_; }

private:
    void fillBag();
    void refillPreview();

    LCG rng_{};
    std::array<PieceType, kBagSize> bag_{};
    int bagIndex_{ kBagSize };  // 触发 refill

    // 内部预览缓冲（避免 pop() 修改 peek() 视图）
    std::array<PieceType, kNextCount + kBagSize> preview_{};
    int queuedCount_{ 0 };  // 已被 pop 消费的数量
};

}  // namespace tetris
