#pragma once

// ============================================================
//  Board — 棋盘存储 + 碰撞 + 消行 + 垃圾行
//  纯数据逻辑，无 LVGL 依赖
// ============================================================

#include "tetrisTypes.hpp"
#include "srs.hpp"

namespace tetris {

class Board {
public:
    Board() = default;

    void clear();
    bool isEmpty() const;

    // 碰撞检测：在位置 (px, py) 处放置方块的 4×4 网格是否会冲突
    bool collides(int px, int py, const MiniBoard& shape) const;

    // 锁定方块到棋盘
    void lockPiece(int px, int py, const MiniBoard& shape, Cell cellType);

    // 检测并消除满行，返回被消的行数
    int clearLines();

    // 注入垃圾行 (Tetris99 风格)
    //   - lines: 注入的行数
    //   - holeColumn: 缺口的列 (-1 = 随机)
    // 行从顶部插入，原有内容上移
    void addGarbage(int lines, int holeColumn = -1);

    // 计算垃圾行需要的格子内容
    Cell cellAt(int x, int y) const { return data_[y][x]; }
    const BoardData& data() const { return data_; }

    // 顶层 y 之上被方块占满即死亡 (用于顶层 0..1 行检测)
    bool isGameOver() const;

private:
    BoardData data_{};
};

}  // namespace tetris
