#include "tetris_renderer.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"
#include <cstring>
#include <algorithm>

static constexpr char TAG[] = "TetrisRenderer";

// ============================================================
//  颜色映射（颜色常量定义在 tetris_renderer.hpp 中）
// ============================================================

lv_color_t TetrisRenderer::pieceToColor(BoardCell val)
{
    if (val == 0) return COLOR_EMPTY;
    if (val == 8) return COLOR_GARBAGE;  // 垃圾行标记
    return pieceTypeToColor(colorToPiece(val));
}

lv_color_t TetrisRenderer::pieceTypeToColor(PieceType type)
{
    switch (type) {
        case PieceType::I: return COLOR_I;
        case PieceType::O: return COLOR_O;
        case PieceType::T: return COLOR_T;
        case PieceType::S: return COLOR_S;
        case PieceType::Z: return COLOR_Z;
        case PieceType::J: return COLOR_J;
        case PieceType::L: return COLOR_L;
        default:           return COLOR_EMPTY;
    }
}

// ============================================================
//  构造 / 析构
// ============================================================

static constexpr lv_coord_t BOARD_PAD = 16;        // 棋盘左边距
static constexpr lv_coord_t BTN_SIZE  = 52;
static constexpr lv_coord_t BTN_GAP   = 6;

TetrisRenderer::TetrisRenderer(Display* display, lv_obj_t* parent, lv_coord_t playerWidth)
    : m_display(display), m_playerWidth(playerWidth)
{
    // 根据可用宽度计算格子尺寸
    // boardPad(16) + 10*cs + sideGap(16) + sidePad(12)*2 + 4*(cs*3/4) = 56 + 13*cs
    m_cellSize = std::max(16, std::min(32,
        static_cast<int>((playerWidth - 56) / 13)));
    // ── 玩家主容器：flex 列，flex_grow 让 flexRow 自动分配等宽 ──
    m_container = lv_obj_create(parent);
    lv_obj_remove_style_all(m_container);
    lv_obj_set_flex_grow(m_container, 1);
    lv_obj_set_height(m_container, LV_PCT(100));
    lv_obj_set_style_border_width(m_container, 1, 0);
    lv_obj_set_style_border_color(m_container, GUI::Color::SUBTLE, 0);
    lv_obj_set_style_border_side(m_container, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_flex_flow(m_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(m_container, BOARD_PAD, 0);
    lv_obj_set_scrollbar_mode(m_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(m_container, LV_DIR_NONE);

    // ── 顶部游戏区：flex 行，棋盘 + 侧栏 ──
    auto gameArea = lv_obj_create(m_container);
    lv_obj_remove_style_all(gameArea);
    lv_obj_set_size(gameArea, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(gameArea, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(gameArea, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(gameArea, BOARD_PAD, 0);
    lv_obj_set_scrollbar_mode(gameArea, LV_SCROLLBAR_MODE_OFF);

    // 初始化 prevState 为不可能值，强制首次渲染检测到变化
    m_prevState.ghostPieceX = -1;
    m_prevState.ghostPieceY = -1;
    m_prevState.currentPieceType = PieceType::NONE;
    for (auto& v : m_prevState.nextPieces) v = PieceType::NONE;
    m_prevState.holdPiece = PieceType::NONE;

    createBoardGrid(gameArea);

    // ── GAME OVER 覆盖层（初始隐藏） ──
    m_gameOverOverlay = lv_obj_create(m_boardContainer);
    lv_obj_set_size(m_gameOverOverlay, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(m_gameOverOverlay, 0, 0);
    lv_obj_set_style_bg_color(m_gameOverOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(m_gameOverOverlay, LV_OPA_60, 0);
    lv_obj_set_style_border_width(m_gameOverOverlay, 0, 0);
    lv_obj_set_style_radius(m_gameOverOverlay, 0, 0);
    lv_obj_add_flag(m_gameOverOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_SCROLLABLE);

    m_gameOverLabel = lv_label_create(m_gameOverOverlay);
    lv_label_set_text(m_gameOverLabel, "GAME\nOVER");
    lv_obj_set_style_text_color(m_gameOverLabel, lv_color_white(), 0);
    lv_obj_set_style_text_font(m_gameOverLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
    lv_obj_set_style_text_align(m_gameOverLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(m_gameOverLabel);

    // ── 侧栏：flex 列，Next 预览 + 信息标签 ──
    auto sidePanel = lv_obj_create(gameArea);
    lv_obj_remove_style_all(sidePanel);
    lv_obj_set_size(sidePanel, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sidePanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(sidePanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(sidePanel, 16, 0);
    lv_obj_set_scrollbar_mode(sidePanel, LV_SCROLLBAR_MODE_OFF);

    createNextPreview(sidePanel);
    createInfoLabels(sidePanel);

    // ── 底部按钮栏 ──
    createTouchButtons(m_container);

    // 初始化视觉缓存（全空）
    std::memset(m_visualCache, 0, sizeof(m_visualCache));
}

TetrisRenderer::~TetrisRenderer()
{
    // LVGL 对象由父容器自动销毁
}

// ============================================================
//  棋盘网格创建
// ============================================================

void TetrisRenderer::createBoardGrid(lv_obj_t* parent)
{
    auto board = lv_obj_create(parent);
    m_boardContainer = board;
    lv_obj_remove_style_all(board);
    lv_obj_set_style_bg_color(board, COLOR_EMPTY, 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_size(board, BOARD_WIDTH * m_cellSize, ROWS * m_cellSize);
    lv_obj_set_style_border_width(board, 1, 0);
    lv_obj_set_style_border_color(board, COLOR_GRID_LINE, 0);
    lv_obj_set_scrollbar_mode(board, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(board, LV_DIR_NONE);

    for (int lvglRow = 0; lvglRow < ROWS; lvglRow++) {
        bool isHidden = (lvglRow < BOARD_HIDDEN_H);  // 顶 2 行为隐藏行
        lv_color_t bgColor = isHidden ? COLOR_HIDDEN : COLOR_EMPTY;
        for (int col = 0; col < BOARD_WIDTH; col++) {
            auto cell = lv_obj_create(board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, m_cellSize, m_cellSize);
            lv_obj_set_pos(cell, col * m_cellSize, lvglRow * m_cellSize);
            lv_obj_set_style_bg_color(cell, bgColor, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, COLOR_GRID_LINE, 0);
            m_cells[lvglRow][col] = cell;
        }
    }
}

// ============================================================
//  Next 预览（原 HOLD 已合并到此面板）
// ============================================================

void TetrisRenderer::createNextPreview(lv_obj_t* parent)
{
    int previewSize = m_cellSize * 3 / 4;
    int gridSize = previewSize * PREVIEW_SIZE;

    // Next 容器 — flex 列，自动居中
    m_nextPanel = lv_obj_create(parent);
    lv_obj_remove_style_all(m_nextPanel);
    lv_obj_set_style_bg_color(m_nextPanel, GUI::Color::CARD, 0);
    lv_obj_set_style_bg_opa(m_nextPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_nextPanel, 8, 0);
    lv_obj_set_style_pad_all(m_nextPanel, 12, 0);
    lv_obj_set_style_pad_row(m_nextPanel, 4, 0);
    lv_obj_set_flex_flow(m_nextPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_nextPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(m_nextPanel, LV_SIZE_CONTENT);

    // 4 个预览槽
    for (int slot = 0; slot < PREVIEW_COUNT; slot++) {
        auto grid = lv_obj_create(m_nextPanel);
        lv_obj_remove_style_all(grid);
        lv_obj_set_size(grid, gridSize, gridSize);
        lv_obj_set_style_bg_color(grid, lv_color_darken(COLOR_EMPTY, 2), 0);
        lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);

        for (int r = 0; r < PREVIEW_SIZE; r++) {
            for (int c = 0; c < PREVIEW_SIZE; c++) {
                auto cell = lv_obj_create(grid);
                lv_obj_remove_style_all(cell);
                lv_obj_set_size(cell, previewSize, previewSize);
                lv_obj_set_pos(cell, c * previewSize, r * previewSize);
                lv_obj_set_style_bg_color(cell, COLOR_EMPTY, 0);
                lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(cell, 1, 0);
                lv_obj_set_style_border_color(cell, COLOR_GRID_LINE, 0);
                m_nextCells[slot][r][c] = cell;
            }
        }
    }
}

// ============================================================
//  棋盘同步
// ============================================================

void TetrisRenderer::syncBoard(const Board& board)
{
    for (int y = 0; y < BOARD_VISIBLE_H; y++) {
        int lvglRow = boardYToLvglRow(y);
        for (int col = 0; col < BOARD_WIDTH; col++) {
            BoardCell val = board.get(col, y);
            // diff: 只更新视觉上有变化的格子
            if (m_visualCache[lvglRow][col] != val) {
                m_visualCache[lvglRow][col] = val;
                applyCellColor(lvglRow, col, pieceToColor(val));
            }
        }
    }
}

void TetrisRenderer::applyCellColor(int lvglRow, int col, lv_color_t color)
{
    if (lvglRow < 0 || lvglRow >= ROWS || col < 0 || col >= BOARD_WIDTH)
        return;
    // border/bg_opa 在 createBoardGrid 中已初始化，运行时只改 bg_color
    lv_obj_set_style_bg_color(m_cells[lvglRow][col], color, 0);
}

// ============================================================
// ============================================================
//  Hold / Next 预览更新
// ============================================================

void TetrisRenderer::clearPreviewGrid(lv_obj_t* grid[PREVIEW_SIZE][PREVIEW_SIZE])
{
    for (int r = 0; r < PREVIEW_SIZE; r++) {
        for (int c = 0; c < PREVIEW_SIZE; c++) {
            lv_obj_set_style_bg_color(grid[r][c], COLOR_EMPTY, 0);
        }
    }
}

void TetrisRenderer::drawPreviewPiece(lv_obj_t* grid[PREVIEW_SIZE][PREVIEW_SIZE],
                                       PieceType type, lv_color_t color)
{
    clearPreviewGrid(grid);

    if (type == PieceType::NONE) return;

    const auto& shape = SRS_SHAPES[static_cast<int>(type)][0];  // R0
    for (int r = 0; r < PREVIEW_SIZE; r++) {
        for (int c = 0; c < PREVIEW_SIZE; c++) {
            if (shape[r][c]) {
                lv_obj_set_style_bg_color(grid[r][c], color, 0);
            }
        }
    }
}

void TetrisRenderer::drawNext(const PieceType preview[4])
{
    for (int slot = 0; slot < PREVIEW_COUNT; slot++) {
        drawPreviewPiece(m_nextCells[slot], preview[slot], pieceTypeToColor(preview[slot]));
    }
}

// ============================================================
//  统计更新
// ============================================================

// ============================================================
//  信息标签创建
// ============================================================

void TetrisRenderer::createInfoLabels(lv_obj_t* parent)
{
    auto makeInfo = [&](lv_obj_t*& label, lv_color_t color) {
        label = lv_label_create(parent);
        lv_label_set_text(label, "");
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    };
    makeInfo(m_attackLabel,  GUI::Color::SUBTLE);
    makeInfo(m_comboLabel,   LV_COLOR_MAKE(0xFF, 0xCC, 0x00));
    makeInfo(m_garbageLabel, LV_COLOR_MAKE(0xFF, 0x44, 0x44));
}

// ============================================================
//  信息栏
// ============================================================

void TetrisRenderer::drawInfo(int combo, int garbageFlash)
{
    // 攻击目标（静态，中文缩窄）
    lv_label_set_text(m_attackLabel, "攻击：其他");
    lv_obj_clear_flag(m_attackLabel, LV_OBJ_FLAG_HIDDEN);

    // 连击（combo=-1 隐藏，0=首次不显示，1+=显示连击 2+）
    if (combo >= 1) {
        char buf[24];
        snprintf(buf, sizeof(buf), "连击x%d", combo + 1);
        lv_label_set_text(m_comboLabel, buf);
        lv_obj_clear_flag(m_comboLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_comboLabel, LV_OBJ_FLAG_HIDDEN);
    }

    // 垃圾行闪烁
    if (garbageFlash > 0) {
        char buf[24];
        snprintf(buf, sizeof(buf), "垃圾+%d", garbageFlash);
        lv_label_set_text(m_garbageLabel, buf);
        lv_obj_clear_flag(m_garbageLabel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(m_garbageLabel, LV_OBJ_FLAG_HIDDEN);
    }
}

// ============================================================
//  触屏按钮创建
// ============================================================

void TetrisRenderer::createTouchButtons(lv_obj_t* parent)
{
    // 按钮栏容器：flex 行，居中
    auto bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, BTN_GAP, 0);
    lv_obj_set_style_pad_top(bar, 12, 0);

    auto makeBtn = [&](const char* text) -> lv_obj_t* {
        auto btn = lv_btn_create(bar);
        lv_obj_set_size(btn, BTN_SIZE, BTN_SIZE);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_bg_color(btn, GUI::Color::CARD, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

        auto label = lv_label_create(btn);
        lv_label_set_text(label, text);
        lv_obj_center(label);
        lv_obj_set_style_text_color(label, GUI::Color::TEXT, 0);

        lv_obj_add_event_cb(btn, onBtnPressed, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(btn, onBtnReleased, LV_EVENT_RELEASED, this);
        return btn;
    };

    m_btnLeft  = makeBtn("<");
    m_btnRight = makeBtn(">");
    m_btnSoft  = makeBtn("v");
    m_btnHard  = makeBtn("V");
    m_btnCCW   = makeBtn("CCW");
    m_btnCW    = makeBtn("CW");
    m_btnHold  = makeBtn("H");
}

// ============================================================
//  按钮回调
// ============================================================

void TetrisRenderer::onBtnPressed(lv_event_t* e)
{
    auto renderer = static_cast<TetrisRenderer*>(lv_event_get_user_data(e));
    auto btn = lv_event_get_target(e);
    auto* p = renderer->m_playerState;
    if (!p) return;

    if (btn == renderer->m_btnLeft)   { p->keyLeft  = true; }
    if (btn == renderer->m_btnRight)  { p->keyRight = true; }
    if (btn == renderer->m_btnSoft)   { p->keySoft  = true; }
    if (btn == renderer->m_btnCW)     { p->keyCW    = true; }
    if (btn == renderer->m_btnCCW)    { p->keyCCW   = true; }
    if (btn == renderer->m_btnHard)   { p->keyHard  = true; }
    if (btn == renderer->m_btnHold)   { p->keyHold  = true; }
}

void TetrisRenderer::onBtnReleased(lv_event_t* e)
{
    auto renderer = static_cast<TetrisRenderer*>(lv_event_get_user_data(e));
    auto btn = lv_event_get_target(e);
    auto* p = renderer->m_playerState;
    if (!p) return;

    if (btn == renderer->m_btnLeft)   { p->keyLeft  = false; p->dasTimer = 0; }
    if (btn == renderer->m_btnRight)  { p->keyRight = false; p->arrTimer = 0; }
    if (btn == renderer->m_btnSoft)   { p->keySoft  = false; }
}

// ============================================================
//  渲染主入口 — 对比 GameState diff 后增量更新
// ============================================================

void TetrisRenderer::syncState()
{
    if (!m_gameState) return;

    const GameState& cur = *m_gameState;

    // ── 棋盘 diff ──
    {
        for (int y = 0; y < BOARD_HEIGHT; y++) {
            int lvglRow = boardYToLvglRow(y);
            for (int col = 0; col < BOARD_WIDTH; col++) {
                BoardCell val = cur.board.get(col, y);
                if (m_visualCache[lvglRow][col] != val) {
                    m_visualCache[lvglRow][col] = val;
                    applyCellColor(lvglRow, col,
                        val ? pieceToColor(val) : emptyColorForRow(lvglRow));
                }
            }
        }
    }

    // ── 方块 / Ghost ──
    {
        bool pieceChanged = false, ghostChanged = false;

        pieceChanged = (cur.currentPieceType != m_prevState.currentPieceType)
                    || (cur.currentPieceX    != m_prevState.currentPieceX)
                    || (cur.currentPieceY    != m_prevState.currentPieceY)
                    || (cur.currentPieceRotation != m_prevState.currentPieceRotation);

        ghostChanged = (cur.ghostPieceX != m_prevState.ghostPieceX)
                     || (cur.ghostPieceY != m_prevState.ghostPieceY)
                     || (cur.currentPieceRotation != m_prevState.currentPieceRotation);

        if (pieceChanged || ghostChanged) {
            lv_color_t color = pieceTypeToColor(cur.currentPieceType);

            // 清除旧 ghost
            if (ghostChanged && m_lastGhostValid) {
                for (int i = 0; i < 4; i++) {
                    int c = m_lastGhostCols[i], r = m_lastGhostRows[i];
                    if (r >= 0 && r < ROWS) {
                        lv_obj_set_style_bg_color(m_cells[r][c],
                            m_visualCache[r][c]
                                ? pieceToColor(m_visualCache[r][c])
                                : emptyColorForRow(r), 0);
                        lv_obj_set_style_bg_opa(m_cells[r][c], LV_OPA_COVER, 0);
                        lv_obj_set_style_border_width(m_cells[r][c], 1, 0);
                        lv_obj_set_style_border_color(m_cells[r][c], COLOR_GRID_LINE, 0);
                    }
                }
                m_lastGhostValid = false;
            }

            // 清除旧方块
            if (pieceChanged && m_lastPieceValid) {
                for (int i = 0; i < 4; i++) {
                    int c = m_lastPieceCols[i], r = m_lastPieceRows[i];
                    if (r >= 0 && r < ROWS) {
                        lv_obj_set_style_bg_color(m_cells[r][c],
                            m_visualCache[r][c]
                                ? pieceToColor(m_visualCache[r][c])
                                : emptyColorForRow(r), 0);
                    }
                }
                m_lastPieceValid = false;
            }

            // 画新 ghost
            if (ghostChanged && cur.currentPieceType != PieceType::NONE) {
                // 从 currentPiece 信息重建 ghost
                Piece ghost(cur.currentPieceType, cur.currentPieceRotation,
                           cur.currentPieceX, cur.currentPieceY);
                int gy = cur.ghostPieceY;
                ghost.setY(gy);
                int cols[4], rows[4];
                ghost.getBlocks(cols, rows);
                for (int i = 0; i < 4; i++) {
                    int r = boardYToLvglRow(rows[i]);
                    m_lastGhostCols[i] = cols[i];
                    m_lastGhostRows[i] = r;
                    if (r >= 0 && r < ROWS) {
                        lv_obj_set_style_bg_color(m_cells[r][cols[i]], color, 0);
                        lv_obj_set_style_bg_opa(m_cells[r][cols[i]], LV_OPA_30, 0);
                        lv_obj_set_style_border_width(m_cells[r][cols[i]], 1, 0);
                        lv_obj_set_style_border_color(m_cells[r][cols[i]], color, 0);
                        lv_obj_set_style_border_opa(m_cells[r][cols[i]], LV_OPA_50, 0);
                    }
                }
                m_lastGhostValid = true;
            }

            // 画新方块
            if (pieceChanged && cur.currentPieceType != PieceType::NONE) {
                // 从 currentPiece 信息重建
                Piece p(cur.currentPieceType, cur.currentPieceRotation,
                        cur.currentPieceX, cur.currentPieceY);
                int cols[4], rows[4];
                p.getBlocks(cols, rows);
                for (int i = 0; i < 4; i++) {
                    int r = boardYToLvglRow(rows[i]);
                    m_lastPieceCols[i] = cols[i];
                    m_lastPieceRows[i] = r;
                    if (r >= 0 && r < ROWS) {
                        applyCellColor(r, cols[i], color);
                        lv_obj_set_style_bg_opa(m_cells[r][cols[i]], LV_OPA_COVER, 0);
                        lv_obj_set_style_border_width(m_cells[r][cols[i]], 1, 0);
                        lv_obj_set_style_border_color(m_cells[r][cols[i]], COLOR_GRID_LINE, 0);
                        lv_obj_set_style_border_opa(m_cells[r][cols[i]], LV_OPA_COVER, 0);
                    }
                }
                m_lastPieceValid = true;
            }
        }
    }

    // ── 预览队列 ──
    {
        bool previewChanged = false;
        for (int s = 0; s < 4; s++) {
            if (cur.nextPieces[s] != m_prevState.nextPieces[s]) {
                previewChanged = true;
                break;
            }
        }
        if (previewChanged) {
            drawNext(cur.nextPieces);
        }
    }

    // ── Hold ──
    {
        if (cur.holdPiece != m_prevState.holdPiece
            || cur.holdUsed != m_prevState.holdUsed) {
            // Hold 显示已合并到预览区第一个槽
            PieceType preview[4];
            for (int s = 0; s < 4; s++) preview[s] = cur.nextPieces[s];
            drawNext(preview);
        }
    }

    // ── 分数 / 信息 ──
    {
        if (cur.score != m_prevState.score
            || cur.combo != m_prevState.combo
            || cur.garbageFlash != m_prevState.garbageFlash) {
            drawInfo(cur.combo, cur.garbageFlash);
        }
    }

    // ── GAME OVER ──
    if (cur.gameOver && !m_prevState.gameOver) {
        lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
    }

    // ── 保存快照供下次 diff ──
    m_prevState = cur;
}
