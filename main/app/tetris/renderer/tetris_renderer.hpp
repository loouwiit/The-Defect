#pragma once

#include "lvgl.h"
#include "display/display.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"
#include "app/tetris/gameLogic/player_state.hpp"

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
    /**
     * @param display     Display 实例
     * @param parent      LVGL 父对象（flex row 容器）
     * @param playerWidth 该玩家区域宽度（分屏用，如 640）
     */
    TetrisRenderer(Display* display, lv_obj_t* parent, lv_coord_t playerWidth);
    ~TetrisRenderer();

    /// 绑定玩家状态（按钮回调直接操作该状态）
    void bindPlayer(PlayerState* state) { m_playerState = state; }

    // ============================================================
    //  棋盘渲染
    // ============================================================

    // 刷新整个棋盘（遍历所有可见格，差异更新颜色）
    void syncBoard(const Board& board);

    // 增量更新 — 仅处理 dirty flags 标记的变化
    void syncDirty(const PlayerState& player, DirtyFlags flags);

    // ============================================================
    //  侧栏渲染
    // ============================================================

    // 更新 Next 队列预览（接收预览数组，4 个预览槽）
    void drawNext(const PieceType preview[4]);

    // 更新信息栏
    void drawInfo(int combo, int garbageFlash);

    // ============================================================
    //  布局
    // ============================================================

    // 获取建议的格子像素大小
    int cellSize() const { return m_cellSize; }

private:
    Display*     m_display{};
    PlayerState* m_playerState = nullptr;  // 绑定的玩家状态
    lv_obj_t*    m_container{};
    int          m_cellSize = 32;
    lv_coord_t   m_playerWidth = 640;

    // 棋盘格: cells[lvglRow][col], lvglRow=0=顶=board y=19
    static constexpr int ROWS = BOARD_VISIBLE_H;  // 20
    lv_obj_t* m_cells[ROWS][BOARD_WIDTH]{};

    // 视觉缓存: 记录上次同步时的格子值，避免重复调用 LVGL API
    BoardCell m_visualCache[ROWS][BOARD_WIDTH]{};

    // 侧栏
    lv_obj_t* m_nextPanel{};
    lv_obj_t* m_attackLabel{};
    lv_obj_t* m_comboLabel{};
    lv_obj_t* m_garbageLabel{};

    // 预览区小格子
    static constexpr int PREVIEW_SIZE = 4;
    static constexpr int PREVIEW_COUNT = 4;
    lv_obj_t* m_nextCells[PREVIEW_COUNT][PREVIEW_SIZE][PREVIEW_SIZE]{};

    // 上次渲染的方块/ghost 位置（供增量清除用）
    int m_lastPieceCols[4]{}, m_lastPieceRows[4]{};
    int m_lastGhostCols[4]{}, m_lastGhostRows[4]{};
    bool m_lastPieceValid = false;
    bool m_lastGhostValid = false;

    // ── 触屏按钮 ──
    lv_obj_t* m_btnLeft{};
    lv_obj_t* m_btnRight{};
    lv_obj_t* m_btnCW{};
    lv_obj_t* m_btnCCW{};
    lv_obj_t* m_btnSoft{};
    lv_obj_t* m_btnHard{};
    lv_obj_t* m_btnHold{};

    void createTouchButtons(lv_obj_t* parent);
    static void onBtnPressed(lv_event_t* e);
    static void onBtnReleased(lv_event_t* e);

    // y 坐标转换: board y → LVGL 行号 (LVGL y 向下为正)
    static int boardYToLvglRow(int boardY) { return ROWS - 1 - boardY; }

    // 内部方法
    static lv_color_t pieceToColor(BoardCell val);
    static lv_color_t pieceTypeToColor(PieceType type);
    void applyCellColor(int lvglRow, int col, lv_color_t color);
    void createBoardGrid(lv_obj_t* parent);
    void createNextPreview(lv_obj_t* parent);
    void createInfoLabels(lv_obj_t* parent);
    void clearPreviewGrid(lv_obj_t* grid[PREVIEW_SIZE][PREVIEW_SIZE]);
    void drawPreviewPiece(lv_obj_t* grid[PREVIEW_SIZE][PREVIEW_SIZE],
                          PieceType type, lv_color_t color);
};
