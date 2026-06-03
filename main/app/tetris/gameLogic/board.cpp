#include "board.hpp"

#include <cstring>

namespace tetris {

void Board::clear() {
    for (auto& row : data_) row.fill(kCellEmpty);
}

bool Board::isEmpty() const {
    for (const auto& row : data_) {
        for (Cell c : row) {
            if (c != kCellEmpty) return false;
        }
    }
    return true;
}

bool Board::collides(int px, int py, const MiniBoard& shape) const {
    for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
            if (shape[sy][sx] == 0) continue;

            int bx = px + sx;
            int by = py + sy;

            // 越界检测
            if (bx < 0 || bx >= kBoardWidth) return true;
            if (by >= kHiddenHeight) return true;
            // 顶部可超出 (py 起始可能为 -1/-2)
            if (by < 0) continue;

            if (data_[by][bx] != kCellEmpty) return true;
        }
    }
    return false;
}

void Board::lockPiece(int px, int py, const MiniBoard& shape, Cell cellType) {
    for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
            if (shape[sy][sx] == 0) continue;
            int bx = px + sx;
            int by = py + sy;
            if (by < 0 || by >= kHiddenHeight) continue;
            if (bx < 0 || bx >= kBoardWidth) continue;
            data_[by][bx] = cellType;
        }
    }
}

int Board::clearLines() {
    int writeY = kHiddenHeight - 1;
    int cleared = 0;

    for (int readY = kHiddenHeight - 1; readY >= 0; --readY) {
        bool full = true;
        for (int x = 0; x < kBoardWidth; ++x) {
            if (data_[readY][x] == kCellEmpty) { full = false; break; }
        }
        if (full) {
            ++cleared;
        } else {
            if (writeY != readY) {
                data_[writeY] = data_[readY];
            }
            --writeY;
        }
    }

    // 顶部空出行清空
    for (int y = writeY; y >= 0; --y) {
        data_[y].fill(kCellEmpty);
    }

    return cleared;
}

void Board::addGarbage(int lines, int holeColumn) {
    if (lines <= 0) return;

    // 计算洞口列（确定性，避免 PRNG 状态污染）
    if (holeColumn < 0 || holeColumn >= kBoardWidth) {
        holeColumn = 0;  // 默认第 0 列
    }

    // 顶部行先腾出
    for (int i = 0; i < lines; ++i) {
        for (int y = 0; y < kHiddenHeight - 1; ++y) {
            data_[y] = data_[y + 1];
        }

        // 在底部添加一行（除了洞口为空）
        auto& bottom = data_[kHiddenHeight - 1];
        bottom.fill(kColorI);  // 灰色
        // 垃圾行使用一个特殊颜色值：使用 kColorI (青色) 即可
        // 但为了在视觉上区分，可以用一个 0x80.. 的值
        // 此处简化：全部填垃圾色，洞口空
        bottom[holeColumn] = kCellEmpty;
    }
}

bool Board::isGameOver() const {
    // 顶层隐藏区 (y=20, 21) 有方块 → 死亡
    for (int y = kBoardHeight; y < kHiddenHeight; ++y) {
        for (int x = 0; x < kBoardWidth; ++x) {
            if (data_[y][x] != kCellEmpty) return true;
        }
    }
    return false;
}

}  // namespace tetris
