#include "pieceQueue.hpp"

#include <algorithm>

namespace tetris {

void PieceQueue::reset(uint32_t seed) {
    rng_ = LCG(seed);
    bagIndex_ = kBagSize;
    queuedCount_ = 0;
    preview_.fill(PieceType::None);
}

void PieceQueue::fillBag() {
    bag_ = {
        PieceType::I, PieceType::O, PieceType::T, PieceType::S,
        PieceType::Z, PieceType::J, PieceType::L
    };

    // Fisher-Yates shuffle with LCG
    for (int i = kBagSize - 1; i > 0; --i) {
        int j = rng_.nextInRange(static_cast<uint32_t>(i + 1));
        std::swap(bag_[i], bag_[j]);
    }
    bagIndex_ = 0;
}

void PieceQueue::refillPreview() {
    // 当剩余预览 < kNextCount 时，补充新 bag
    // preview_ 总长度 = kNextCount + kBagSize，确保总能取到 kNextCount 个
    // 每次 pop 一个后，queuedCount_++；当剩余 < kNextCount 时重填

    if (queuedCount_ + kNextCount <= static_cast<int>(preview_.size())) {
        return;  // 还够用
    }

    // 重置：把剩余的方块移到底部，重新填充
    int remaining = static_cast<int>(preview_.size()) - queuedCount_;
    for (int i = 0; i < remaining; ++i) {
        preview_[i] = preview_[queuedCount_ + i];
    }
    queuedCount_ = 0;

    // 补充新 bag 到剩余位置之后
    int writeIdx = remaining;
    while (writeIdx < static_cast<int>(preview_.size())) {
        if (bagIndex_ >= kBagSize) fillBag();
        preview_[writeIdx++] = bag_[bagIndex_++];
    }
}

PieceType PieceQueue::pop() {
    refillPreview();
    PieceType t = preview_[queuedCount_];
    preview_[queuedCount_] = PieceType::None;  // 释放
    ++queuedCount_;
    return t;
}

PieceType PieceQueue::peek(int index) const {
    if (index < 0 || index >= kNextCount) return PieceType::None;
    int idx = queuedCount_ + index;
    if (idx < 0 || idx >= static_cast<int>(preview_.size())) {
        return PieceType::None;
    }
    return preview_[idx];
}

}  // namespace tetris
