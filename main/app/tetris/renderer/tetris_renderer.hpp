#pragma once

#include <functional>
#include "lvgl.h"
#include "display/display.hpp"
#include "app/tetris/gameLogic/tetris_client.hpp"
#include "app/tetris/gameLogic/gameState.hpp"
#include "app/tetris/gameLogic/player_state.hpp"

/// 每 (hostId, playerId) 连续输入暂存（供 gameLoop OR 合并）
struct HostContinuousInput {
    bool left  = false;
    bool right = false;
    bool soft  = false;
};

/**
 * @brief Tetris LVGL 渲染器
 *
 * 职责：将 GameState 渲染到 LVGL 对象树。
 * 不包含任何游戏逻辑，只做可视化。
 *
 * 数据源：绑定一个 GameState*，syncState() 从中读取并 diff 渲染。
 * 按钮回调：绑定 PlayerState* 用于写入按键状态。
 *
 * 线程安全：所有方法必须在 Display::LockGuard 保护下调用。
 */
class TetrisRenderer {
public:
    // ── Guideline 标准色 ──
    static constexpr lv_color_t COLOR_I      = LV_COLOR_MAKE(0x00, 0xFF, 0xFF);
    static constexpr lv_color_t COLOR_O      = LV_COLOR_MAKE(0xFF, 0xFF, 0x00);
    static constexpr lv_color_t COLOR_T      = LV_COLOR_MAKE(0xAA, 0x00, 0xFF);
    static constexpr lv_color_t COLOR_S      = LV_COLOR_MAKE(0x00, 0xFF, 0x00);
    static constexpr lv_color_t COLOR_Z      = LV_COLOR_MAKE(0xFF, 0x00, 0x00);
    static constexpr lv_color_t COLOR_J      = LV_COLOR_MAKE(0x00, 0x00, 0xFF);
    static constexpr lv_color_t COLOR_L      = LV_COLOR_MAKE(0xFF, 0x88, 0x00);
    static constexpr lv_color_t COLOR_GHOST  = LV_COLOR_MAKE(0x88, 0x88, 0x88);
    static constexpr lv_color_t COLOR_GARBAGE = LV_COLOR_MAKE(0x55, 0x55, 0x55);
    static constexpr lv_color_t COLOR_EMPTY  = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e);
    static constexpr lv_color_t COLOR_HIDDEN = LV_COLOR_MAKE(0x0e, 0x0e, 0x1e);
    static constexpr lv_color_t COLOR_GRID_LINE = LV_COLOR_MAKE(0x22, 0x22, 0x38);

    TetrisRenderer(Display* display, lv_obj_t* parent, lv_coord_t playerWidth);
    ~TetrisRenderer();

    /// 绑定 GameState（渲染数据源）
    void bindGameState(const GameState* state) { m_gameState = state; }

    /// 绑定 PlayerState（仅触屏按钮回调需要写输入）
    void bindPlayer(PlayerState* state) { m_playerState = state; }

    /// 绑定 hostInput 缓冲区（连续输入走 OR 合并通道，hostId=0=本机触屏）
    void bindHostInput(HostContinuousInput* buf) { m_hostInputBuf = buf; }

    /// 设置攻击目标切换回调（触屏点击攻击标签时调用）
    void setOnCycleTarget(std::function<void()> cb) { m_onCycleTarget = cb; }

    /// 设置退出游戏回调
    void setOnExit(std::function<void()> cb) { m_onExit = cb; }

    // ============================================================
    //  渲染主入口
    // ============================================================

    /// 从绑定的 GameState 同步渲染（内部做 diff）
    void syncState();

    // ============================================================
    //  布局
    // ============================================================

    int cellSize() const { return m_cellSize; }
    lv_obj_t* getContainer() { return m_container; }

private:
    Display*       m_display{};
    const GameState* m_gameState = nullptr;   // 渲染数据源
    PlayerState*   m_playerState = nullptr;    // 仅按钮回调用
    HostContinuousInput* m_hostInputBuf = nullptr; // 连续输入缓冲区（触屏写到此）
    lv_obj_t*      m_container{};
    lv_obj_t*      m_boardContainer{};        // 棋盘容器（用于 overlay 定位）
    lv_obj_t*      m_gameOverOverlay{};       // GAME OVER 遮罩
    lv_obj_t*      m_gameOverLabel{};
    int            m_cellSize = 32;
    lv_coord_t     m_playerWidth = 640;

    // 棋盘: 单个 canvas（PSRAM RGB565 缓冲），lvglRow=0=顶=board y=21
    static constexpr int ROWS = BOARD_HEIGHT;
    lv_obj_t*      m_boardCanvas{};
    lv_draw_buf_t* m_boardDrawBuf{};   // canvas 内部 draw_buf（取 header.stride）
    uint8_t*       m_boardBuf{};       // PSRAM 像素缓冲（析构释放）
    size_t         m_boardStride = 0;
    int            m_boardW = 0, m_boardH = 0;

    // 视觉缓存: 上次渲染的棋盘值，用于 diff
    BoardCell m_visualCache[ROWS][BOARD_WIDTH]{};

    // 上次渲染的 GameState 快照（用于增量对比）
    GameState m_prevState;

    // 侧栏
    lv_obj_t* m_nextPanel{};
    lv_obj_t* m_attackLabel{};
    lv_obj_t* m_comboLabel{};
    lv_obj_t* m_garbageLabel{};

    // 预览区: 每槽 1 个 canvas（PSRAM RGB565 缓冲）
    static constexpr int PREVIEW_SIZE = 4;
    static constexpr int PREVIEW_COUNT = 4;
    lv_obj_t*      m_nextCanvases[PREVIEW_COUNT]{};
    lv_draw_buf_t* m_nextDrawBufs[PREVIEW_COUNT]{};
    uint8_t*       m_nextBufs[PREVIEW_COUNT]{};
    size_t         m_nextStride = 0;
    int            m_previewPixelSize = 0;   // 每预览格像素尺寸

    // 上次渲染的方块/ghost 位置（供增量清除用）
    int m_lastPieceCols[4]{}, m_lastPieceRows[4]{};
    int m_lastGhostCols[4]{}, m_lastGhostRows[4]{};
    bool m_lastPieceValid = false;
    bool m_lastGhostValid = false;
    bool m_initialInvalidated = false;   // 首次 syncState 强制失效所有 canvas 上屏

    // 攻击目标切换回调
    std::function<void()> m_onCycleTarget;
    std::function<void()> m_onExit;

    // ── 触屏按钮 ──
    lv_obj_t* m_btnLeft{};
    lv_obj_t* m_btnRight{};
    lv_obj_t* m_btnCW{};
    lv_obj_t* m_btnCCW{};
    lv_obj_t* m_btnSoft{};
    lv_obj_t* m_btnHard{};
    lv_obj_t* m_btnHold{};
    lv_obj_t* m_btnExit{};

    void createTouchButtons(lv_obj_t* parent);
    static void onBtnPressed(lv_event_t* e);
    static void onBtnReleased(lv_event_t* e);

    static int boardYToLvglRow(int boardY) { return ROWS - 1 - boardY; }

    /// 根据行号返回空格颜色（隐藏行用更暗色）
    static lv_color_t emptyColorForRow(int lvglRow) {
        return (lvglRow < BOARD_HIDDEN_H) ? COLOR_HIDDEN : COLOR_EMPTY;
    }

    static lv_color_t pieceToColor(BoardCell val);
    static lv_color_t pieceTypeToColor(PieceType type);
    void createBoardGrid(lv_obj_t* parent);
    void createNextPreview(lv_obj_t* parent);
    void createInfoLabels(lv_obj_t* parent);

    // ── canvas 像素缓冲绘制辅助 ──
    void paintBoardCell(int lvglRow, int col, lv_color_t color, lv_opa_t opa);
    void paintPreviewCell(int slot, int r, int c, lv_color_t color, lv_opa_t opa);
    void clearPreviewSlot(int slot);                 // 清空一个预览槽
    void drawPreviewPiece(int slot, PieceType type, lv_color_t color);
    void invalidateBoardLocal(lv_area_t* local);     // 失效棋盘本地子区域（自动转屏幕坐标）

    // 内部渲染辅助方法
    void drawNext(const PieceType preview[4]);
    void drawInfo(int combo, int garbageFlash, int attackTarget);
};
