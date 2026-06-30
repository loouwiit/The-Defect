#include "tetris_renderer.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"
#include <cstring>

static constexpr char TAG[] = "TetrisRenderer";

// ============================================================
//  Guideline 标准色
// ============================================================

static constexpr lv_color_t COLOR_I   = LV_COLOR_MAKE(0x00, 0xFF, 0xFF);  // 青
static constexpr lv_color_t COLOR_O   = LV_COLOR_MAKE(0xFF, 0xFF, 0x00);  // 黄
static constexpr lv_color_t COLOR_T   = LV_COLOR_MAKE(0xAA, 0x00, 0xFF);  // 紫
static constexpr lv_color_t COLOR_S   = LV_COLOR_MAKE(0x00, 0xFF, 0x00);  // 绿
static constexpr lv_color_t COLOR_Z   = LV_COLOR_MAKE(0xFF, 0x00, 0x00);  // 红
static constexpr lv_color_t COLOR_J   = LV_COLOR_MAKE(0x00, 0x00, 0xFF);  // 蓝
static constexpr lv_color_t COLOR_L   = LV_COLOR_MAKE(0xFF, 0x88, 0x00);  // 橙
static constexpr lv_color_t COLOR_GHOST = LV_COLOR_MAKE(0x88, 0x88, 0x88); // 灰
static constexpr lv_color_t COLOR_GARBAGE = LV_COLOR_MAKE(0x55, 0x55, 0x55); // 暗灰
static constexpr lv_color_t COLOR_EMPTY   = LV_COLOR_MAKE(0x1a, 0x1a, 0x2e); // 背景色
static constexpr lv_color_t COLOR_GRID_LINE = LV_COLOR_MAKE(0x22, 0x22, 0x38); // 网格线

// ============================================================
//  颜色映射
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

TetrisRenderer::TetrisRenderer(Display* display, lv_obj_t* parent)
    : m_display(display)
{
    m_container = lv_obj_create(parent);
    lv_obj_remove_style_all(m_container);
    lv_obj_set_style_bg_opa(m_container, LV_OPA_TRANSP, 0);
    lv_obj_set_size(m_container, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollbar_mode(m_container, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(m_container, LV_DIR_NONE);

    createBoardGrid();
    createNextPreview();

    // 初始化视觉缓存（全空）
    std::memset(m_visualCache, 0, sizeof(m_visualCache));

    int boardX = 16;
    int boardY = 16;
    int sideX = boardX + BOARD_WIDTH * m_cellSize + 16;

    // 定位棋盘
    auto boardContainer = lv_obj_get_child(m_container, 0);
    if (boardContainer) lv_obj_set_pos(boardContainer, boardX, boardY);

    // NEXT 面板
    lv_obj_align(m_nextPanel, LV_ALIGN_TOP_LEFT, sideX, boardY);

    // 信息栏 — 独立区域，NEXT 面板下方
    auto makeInfo = [&](lv_obj_t*& label, lv_color_t color) {
        label = lv_label_create(m_container);
        lv_label_set_text(label, "");
        lv_obj_set_style_text_color(label, color, 0);
        lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    };
    makeInfo(m_attackLabel,  GUI::Color::SUBTLE);
    makeInfo(m_comboLabel,   LV_COLOR_MAKE(0xFF, 0xCC, 0x00));
    makeInfo(m_garbageLabel, LV_COLOR_MAKE(0xFF, 0x44, 0x44));

    // 全部相对于 NEXT 面板定位，各自独立互不影响
    lv_obj_align_to(m_attackLabel,  m_nextPanel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 6);
    lv_obj_align_to(m_comboLabel,   m_nextPanel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 32);
    lv_obj_align_to(m_garbageLabel, m_nextPanel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 58);
}

TetrisRenderer::~TetrisRenderer()
{
    // LVGL 对象由父容器自动销毁
}

// ============================================================
//  棋盘网格创建
// ============================================================

void TetrisRenderer::createBoardGrid()
{
    int boardW = BOARD_WIDTH * m_cellSize;
    int boardH = ROWS * m_cellSize;

    auto board = lv_obj_create(m_container);
    lv_obj_remove_style_all(board);
    lv_obj_set_style_bg_color(board, COLOR_EMPTY, 0);
    lv_obj_set_style_bg_opa(board, LV_OPA_COVER, 0);
    lv_obj_set_size(board, boardW, boardH);
    lv_obj_set_pos(board, 0, 0);
    lv_obj_set_style_border_width(board, 1, 0);
    lv_obj_set_style_border_color(board, COLOR_GRID_LINE, 0);
    lv_obj_set_scrollbar_mode(board, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(board, LV_DIR_NONE);

    for (int lvglRow = 0; lvglRow < ROWS; lvglRow++) {
        for (int col = 0; col < BOARD_WIDTH; col++) {
            auto cell = lv_obj_create(board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, m_cellSize, m_cellSize);
            lv_obj_set_pos(cell, col * m_cellSize, lvglRow * m_cellSize);
            lv_obj_set_style_bg_color(cell, COLOR_EMPTY, 0);
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

void TetrisRenderer::createNextPreview()
{
    int previewSize = m_cellSize * 3 / 4;
    int gridSize = previewSize * PREVIEW_SIZE;

    // Next 容器 — flex 列，自动居中
    m_nextPanel = lv_obj_create(m_container);
    lv_obj_remove_style_all(m_nextPanel);
    lv_obj_set_style_bg_color(m_nextPanel, GUI::Color::CARD, 0);
    lv_obj_set_style_bg_opa(m_nextPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_nextPanel, 8, 0);
    lv_obj_set_style_pad_all(m_nextPanel, 12, 0);
    lv_obj_set_style_pad_row(m_nextPanel, 4, 0);
    lv_obj_set_flex_flow(m_nextPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(m_nextPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_height(m_nextPanel, LV_SIZE_CONTENT);

    // 4 个预览槽（无标题，玩家通过方块变化自然理解）
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
    auto cell = m_cells[lvglRow][col];
    lv_obj_set_style_bg_color(cell, color, 0);
    // 重置为不透明（覆盖 Ghost 可能设置的半透明）
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    // 重置边框为网格线（覆盖 Ghost 可能设置的彩色边框）
    lv_obj_set_style_border_width(cell, 1, 0);
    lv_obj_set_style_border_color(cell, COLOR_GRID_LINE, 0);
    lv_obj_set_style_border_opa(cell, LV_OPA_COVER, 0);
}

// ============================================================
//  活动方块 / Ghost 绘制
// ============================================================

void TetrisRenderer::drawPiece(const Piece& piece, BoardCell color)
{
    int cols[4], yCoords[4];
    piece.getBlocks(cols, yCoords);
    lv_color_t lvColor = pieceToColor(color);

    for (int i = 0; i < 4; i++) {
        int y = yCoords[i];
        if (y < 0 || y >= BOARD_VISIBLE_H) continue;  // 跳过隐藏区
        int lvglRow = boardYToLvglRow(y);
        applyCellColor(lvglRow, cols[i], lvColor);
    }

    // 记录位置供擦除用
    m_prevPieceX = piece.x();
    m_prevPieceY = piece.y();
    m_prevPieceRot = piece.rotation();
    m_prevPieceType = piece.type();
}

void TetrisRenderer::clearPiece(const Piece& piece)
{
    int cols[4], yCoords[4];
    piece.getBlocks(cols, yCoords);

    for (int i = 0; i < 4; i++) {
        int y = yCoords[i];
        if (y < 0 || y >= BOARD_VISIBLE_H) continue;
        int lvglRow = boardYToLvglRow(y);
        // 从 visual cache 恢复棋盘底色（可能是空或已锁定块）
        applyCellColor(lvglRow, cols[i], pieceToColor(m_visualCache[lvglRow][cols[i]]));
    }
}

void TetrisRenderer::drawGhost(const Piece& ghost, BoardCell color)
{
    int cols[4], yCoords[4];
    ghost.getBlocks(cols, yCoords);
    lv_color_t lvColor = pieceToColor(color);

    for (int i = 0; i < 4; i++) {
        int y = yCoords[i];
        if (y < 0 || y >= BOARD_VISIBLE_H) continue;
        int lvglRow = boardYToLvglRow(y);
        // Ghost: 半透明 + 边框
        lv_obj_set_style_bg_opa(m_cells[lvglRow][cols[i]], LV_OPA_30, 0);
        lv_obj_set_style_bg_color(m_cells[lvglRow][cols[i]], lvColor, 0);
        lv_obj_set_style_border_width(m_cells[lvglRow][cols[i]], 1, 0);
        lv_obj_set_style_border_color(m_cells[lvglRow][cols[i]], lvColor, 0);
        lv_obj_set_style_border_opa(m_cells[lvglRow][cols[i]], LV_OPA_50, 0);
    }

    m_prevGhostY = ghost.y();
}

void TetrisRenderer::clearGhost(const Piece& ghost)
{
    int cols[4], yCoords[4];
    ghost.getBlocks(cols, yCoords);

    for (int i = 0; i < 4; i++) {
        int y = yCoords[i];
        if (y < 0 || y >= BOARD_VISIBLE_H) continue;
        int lvglRow = boardYToLvglRow(y);
        // 恢复不透明度和边框，同时从 cache 恢复棋盘底色
        applyCellColor(lvglRow, cols[i], pieceToColor(m_visualCache[lvglRow][cols[i]]));
    }
}

// ============================================================
//  消行闪烁
// ============================================================

void TetrisRenderer::flashLines(const int /*clearedY*/[4], int /*count*/)
{
    // 已弃用 — 消行不闪烁，直接由 syncBoard 刷新
}

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
//  区域设置
// ============================================================

void TetrisRenderer::setArea(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h)
{
    lv_obj_set_pos(m_container, x, y);
    lv_obj_set_size(m_container, w, h);
}
