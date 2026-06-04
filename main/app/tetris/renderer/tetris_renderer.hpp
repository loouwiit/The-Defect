#pragma once

#include "lvgl.h"
#include "display/display.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"

/**
 * @brief Tetris LVGL 渲染器
 *
 * 职责：将 Board/Piece 状态渲染到 LVGL 对象树。
 * 不包含任何游戏逻辑，只做可视化。
 *
 * 线程安全：所有方法必须在 Display::LockGuard 保护下调用。
 */
class TetrisRenderer {
public:
    TetrisRenderer(Display* display, lv_obj_t* parent);
    ~TetrisRenderer();

    // ============================================================
    //  棋盘渲染
    // ============================================================

    // 刷新整个棋盘（遍历所有可见格，差异更新颜色）
    void syncBoard(const Board& board);

    // 绘制活动方块 + Ghost（直接画在棋盘格上）
    void drawPiece(const Piece& piece, BoardCell color);
    void drawGhost(const Piece& ghost, BoardCell color);

    // 擦除活动方块 + Ghost
    void clearPiece(const Piece& piece);
    void clearGhost(const Piece& ghost);

    // 消行动画 (已弃用)
    void flashLines(const int clearedY[4], int count);

    // ============================================================
    //  侧栏渲染
    // ============================================================

    // 更新 Hold 预览
    void drawHold(PieceType type, bool used);

    // 更新 Next 队列预览
    void drawNext(const PieceQueue& queue);

    // 更新分数/行数/等级
    void drawStats(int score, int lines, int level);

    // 更新垃圾行指示器
    void drawGarbage(int pending);

    // ============================================================
    //  布局
    // ============================================================

    // 设置渲染区域（分屏用）
    void setArea(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);

    // 获取建议的格子像素大小
    int cellSize() const { return m_cellSize; }

private:
    Display*   m_display{};
    lv_obj_t*  m_container{};
    int        m_cellSize = 32;

    // 棋盘格: cells[lvglRow][col], lvglRow=0=顶=board y=19
    static constexpr int ROWS = BOARD_VISIBLE_H;  // 20
    lv_obj_t* m_cells[ROWS][BOARD_WIDTH]{};

    // 视觉缓存: 记录上次同步时的格子值，避免重复调用 LVGL API
    BoardCell m_visualCache[ROWS][BOARD_WIDTH]{};

    // 侧栏
    lv_obj_t* m_holdPanel{};
    lv_obj_t* m_nextPanel{};

    // 预览区小格子
    static constexpr int PREVIEW_SIZE = 4;
    static constexpr int PREVIEW_COUNT = 4;
    lv_obj_t* m_holdCells[PREVIEW_SIZE][PREVIEW_SIZE]{};
    lv_obj_t* m_nextCells[PREVIEW_COUNT][PREVIEW_SIZE][PREVIEW_SIZE]{};

    // 前帧活动方块位置（用于局部擦除）
    int m_prevPieceX = -100;
    int m_prevPieceY = -100;
    Rotation m_prevPieceRot = Rotation::R0;
    PieceType m_prevPieceType = PieceType::NONE;
    int m_prevGhostY = -100;

    // y 坐标转换: board y → LVGL 行号 (LVGL y 向下为正)
    static int boardYToLvglRow(int boardY) { return ROWS - 1 - boardY; }

    // 内部方法
    static lv_color_t pieceToColor(BoardCell val);
    static lv_color_t pieceTypeToColor(PieceType type);
    void applyCellColor(int lvglRow, int col, lv_color_t color);
    void createBoardGrid();
    void createHoldPreview();
    void createNextPreview();
    void clearPreviewGrid(lv_obj_t* grid[PREVIEW_SIZE][PREVIEW_SIZE]);
    void drawPreviewPiece(lv_obj_t* grid[PREVIEW_SIZE][PREVIEW_SIZE],
                          PieceType type, lv_color_t color);
};
