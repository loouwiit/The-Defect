#include "tetris_renderer.hpp"
#include "display/font.hpp"
#include "gui/gui.hpp"
#include <esp_heap_caps.h>
#include <cstring>
#include <cassert>
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
	switch (type)
	{
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
static constexpr lv_coord_t BTN_SIZE = 52;
static constexpr lv_coord_t BTN_GAP = 6;

TetrisRenderer::TetrisRenderer(Display* display, lv_obj_t* parent, lv_coord_t playerWidth)
	: m_display(display), m_playerWidth(playerWidth)

{
	// 根据可用宽度计算格子尺寸
	// boardPad(16) + 10*cs + sideGap(16) + sidePad(12)*2 + 4*(cs*3/4) = 56 + 13*cs
	// 同时受高度限制：pad(16) + 22*cs + 按钮(~64) <= 720 → cs <= 29
	m_cellSize = std::max(16, std::min(29,
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

	// ── 退出按钮（侧栏顶部） ──
	m_btnExit = lv_btn_create(sidePanel);
	lv_obj_set_size(m_btnExit, LV_SIZE_CONTENT, 32);
	lv_obj_set_style_radius(m_btnExit, 6, 0);
	lv_obj_set_style_bg_color(m_btnExit, lv_color_hex(0xAA3333), 0);
	lv_obj_set_style_bg_opa(m_btnExit, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_btnExit, 0, 0);
	lv_obj_set_style_pad_left(m_btnExit, 12, 0);
	lv_obj_set_style_pad_right(m_btnExit, 12, 0);
	lv_obj_set_style_margin_top(m_btnExit, 8, 0);
	lv_obj_set_style_margin_bottom(m_btnExit, 8, 0);
	auto exitLbl = lv_label_create(m_btnExit);
	lv_label_set_text(exitLbl, "退出");
	lv_obj_center(exitLbl);
	lv_obj_set_style_text_color(exitLbl, lv_color_white(), 0);
	lv_obj_set_style_text_font(exitLbl, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_add_event_cb(m_btnExit, [](lv_event_t* e)
		{
			auto* self = static_cast<TetrisRenderer*>(lv_event_get_user_data(e));
			if (self->m_onExit) self->m_onExit();
		}, LV_EVENT_CLICKED, this);

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
	heap_caps_free(m_boardBuf);
	for (int i = 0; i < PREVIEW_COUNT; i++)
		heap_caps_free(m_nextBufs[i]);
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

	// ── 棋盘渲染：单个 canvas（PSRAM RGB565 缓冲），替代 220 个 cell 对象 ──
	m_boardW = BOARD_WIDTH * m_cellSize;
	m_boardH = ROWS * m_cellSize;
	const size_t bufSize = (size_t)m_boardW * m_boardH * 2;
	m_boardBuf = static_cast<uint8_t*>(
		heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	assert(m_boardBuf);

	m_boardCanvas = lv_canvas_create(board);
	lv_obj_remove_style_all(m_boardCanvas);
	lv_obj_set_pos(m_boardCanvas, 0, 0);
	lv_canvas_set_buffer(m_boardCanvas, m_boardBuf, m_boardW, m_boardH,
		LV_COLOR_FORMAT_RGB565);
	m_boardDrawBuf = lv_canvas_get_draw_buf(m_boardCanvas);
	m_boardStride = m_boardDrawBuf->header.stride;

	// 初始整板填充空色 + 网格线（与原先逐 cell 边框视觉一致）
	for (int lvglRow = 0; lvglRow < ROWS; lvglRow++)
		for (int col = 0; col < BOARD_WIDTH; col++)
			paintBoardCell(lvglRow, col, emptyColorForRow(lvglRow), LV_OPA_COVER);
}

// ============================================================
//  Next 预览（原 HOLD 已合并到此面板）
// ============================================================

void TetrisRenderer::createNextPreview(lv_obj_t* parent)

{
	m_previewPixelSize = m_cellSize * 3 / 4;
	int gridSize = m_previewPixelSize * PREVIEW_SIZE;

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

	// 4 个预览槽 — 每槽 1 个 canvas（PSRAM RGB565 缓冲）
	for (int slot = 0; slot < PREVIEW_COUNT; slot++)
	{
		const size_t bufSize = (size_t)gridSize * gridSize * 2;
		m_nextBufs[slot] = static_cast<uint8_t*>(
			heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		assert(m_nextBufs[slot]);

		auto cv = lv_canvas_create(m_nextPanel);
		lv_obj_remove_style_all(cv);
		lv_obj_set_size(cv, gridSize, gridSize);
		lv_canvas_set_buffer(cv, m_nextBufs[slot], gridSize, gridSize,
			LV_COLOR_FORMAT_RGB565);
		m_nextCanvases[slot] = cv;
		m_nextDrawBufs[slot] = lv_canvas_get_draw_buf(cv);
		if (m_nextStride == 0)
			m_nextStride = m_nextDrawBufs[slot]->header.stride;

		// 初始画空格 + 网格线（格子全覆盖，替代原 lv_color_darken 底色）
		clearPreviewSlot(slot);
	}
}

// ============================================================
//  canvas 像素缓冲绘制辅助
// ============================================================

/// RGB565 混合: 将 src 以 opa(0-255) 叠加到 dst 上
static inline uint16_t blend565(uint16_t dst, uint16_t src, lv_opa_t opa)
{
	if (opa >= LV_OPA_COVER) return src;
	if (opa <= LV_OPA_TRANSP) return dst;
	uint32_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
	uint32_t sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
	uint32_t inv = 255 - opa;
	uint32_t r = (sr * opa + dr * inv) / 255;
	uint32_t g = (sg * opa + dg * inv) / 255;
	uint32_t b = (sb * opa + db * inv) / 255;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

/// 画棋盘一格（填色 + 1px 边框）；opa<COVER 时先恢复空格底色再半透明叠色（ghost 语义）
void TetrisRenderer::paintBoardCell(int lvglRow, int col, lv_color_t color, lv_opa_t opa)

{
	if (lvglRow < 0 || lvglRow >= ROWS || col < 0 || col >= BOARD_WIDTH)
		return;
	if (!m_boardBuf) return;

	const int cs = m_cellSize;
	uint8_t* base = m_boardBuf + (size_t)(lvglRow * cs) * m_boardStride
		+ (size_t)(col * cs) * 2;
	uint16_t v = lv_color_to_u16(color);
	uint16_t grid = lv_color_to_u16(COLOR_GRID_LINE);

	if (opa >= LV_OPA_COVER)
	{
		// 实色填充 + 网格线边框
		for (int y = 0; y < cs; y++)
		{
			uint16_t* p = (uint16_t*)(base + (size_t)y * m_boardStride);
			for (int x = 0; x < cs; x++) p[x] = v;
		}
		for (int x = 0; x < cs; x++)
		{
			((uint16_t*)base)[x] = grid;
			((uint16_t*)(base + (size_t)(cs - 1) * m_boardStride))[x] = grid;
		}
		for (int y = 0; y < cs; y++)
		{
			uint16_t* p = (uint16_t*)(base + (size_t)y * m_boardStride);
			p[0] = grid;
			p[cs - 1] = grid;
		}
	}
	else
	{
		// ghost: 先恢复空格底色+网格，再以 opa 叠色、边框 50% 叠色（与原 bg_opa/border_opa 语义一致）
		uint16_t baseCol = lv_color_to_u16(emptyColorForRow(lvglRow));
		for (int y = 0; y < cs; y++)
		{
			uint16_t* p = (uint16_t*)(base + (size_t)y * m_boardStride);
			for (int x = 0; x < cs; x++) p[x] = baseCol;
			p[0] = grid;
			p[cs - 1] = grid;
		}
		for (int x = 0; x < cs; x++)
		{
			((uint16_t*)base)[x] = grid;
			((uint16_t*)(base + (size_t)(cs - 1) * m_boardStride))[x] = grid;
		}
		for (int y = 0; y < cs; y++)
		{
			uint16_t* p = (uint16_t*)(base + (size_t)y * m_boardStride);
			for (int x = 0; x < cs; x++) p[x] = blend565(p[x], v, opa);
			p[0] = blend565(p[0], v, LV_OPA_50);
			p[cs - 1] = blend565(p[cs - 1], v, LV_OPA_50);
		}
		for (int x = 0; x < cs; x++)
		{
			uint16_t* top = (uint16_t*)base;
			top[x] = blend565(top[x], v, LV_OPA_50);
			uint16_t* bot = (uint16_t*)(base + (size_t)(cs - 1) * m_boardStride);
			bot[x] = blend565(bot[x], v, LV_OPA_50);
		}
	}
}

// ============================================================
//  Hold / Next 预览更新（每槽 1 个 canvas）
// ============================================================

/// 画预览槽一格（填色 + 1px 边框）
void TetrisRenderer::paintPreviewCell(int slot, int r, int c, lv_color_t color, lv_opa_t opa)

{
	if (slot < 0 || slot >= PREVIEW_COUNT || !m_nextBufs[slot]) return;
	const int ps = m_previewPixelSize;
	uint8_t* base = m_nextBufs[slot] + (size_t)(r * ps) * m_nextStride
		+ (size_t)(c * ps) * 2;
	uint16_t v = lv_color_to_u16(color);
	uint16_t grid = lv_color_to_u16(COLOR_GRID_LINE);

	for (int y = 0; y < ps; y++)
	{
		uint16_t* p = (uint16_t*)(base + (size_t)y * m_nextStride);
		for (int x = 0; x < ps; x++)
			p[x] = (opa >= LV_OPA_COVER) ? v : blend565(p[x], v, opa);
	}
	for (int x = 0; x < ps; x++)
	{
		((uint16_t*)base)[x] = grid;
		((uint16_t*)(base + (size_t)(ps - 1) * m_nextStride))[x] = grid;
	}
	for (int y = 0; y < ps; y++)
	{
		uint16_t* p = (uint16_t*)(base + (size_t)y * m_nextStride);
		p[0] = grid;
		p[ps - 1] = grid;
	}
}

/// 清空一个预览槽（画满空格 + 网格线）
void TetrisRenderer::clearPreviewSlot(int slot)

{
	if (slot < 0 || slot >= PREVIEW_COUNT) return;
	for (int r = 0; r < PREVIEW_SIZE; r++)
		for (int c = 0; c < PREVIEW_SIZE; c++)
			paintPreviewCell(slot, r, c, COLOR_EMPTY, LV_OPA_COVER);
}

/// 绘制某个预览槽的方块形状
void TetrisRenderer::drawPreviewPiece(int slot, PieceType type, lv_color_t color)

{
	clearPreviewSlot(slot);

	if (type == PieceType::NONE) return;

	const auto& shape = SRS_SHAPES[static_cast<int>(type)][0];  // R0
	for (int r = 0; r < PREVIEW_SIZE; r++)
	{
		for (int c = 0; c < PREVIEW_SIZE; c++)
		{
			if (shape[r][c])
			{
				paintPreviewCell(slot, r, c, color, LV_OPA_COVER);
			}
		}
	}
}

/// 失效棋盘 canvas 的本地子区域（lv_obj_invalidate_area 需要屏幕坐标，这里自动换算）
void TetrisRenderer::invalidateBoardLocal(lv_area_t* local)

{
	if (!m_boardCanvas) return;
	lv_area_t c;
	lv_obj_get_coords(m_boardCanvas, &c);
	lv_area_t scr;
	lv_area_set(&scr, c.x1 + local->x1, c.y1 + local->y1,
		c.x1 + local->x2, c.y1 + local->y2);
	lv_obj_invalidate_area(m_boardCanvas, &scr);
}

void TetrisRenderer::drawNext(const PieceType preview[4])

{
	// 调用方已持 LVGL 锁；预览槽小且低频变化，整面板重绘（每槽 1 次失效 ≤4 条，不溢出）
	for (int slot = 0; slot < PREVIEW_COUNT; slot++)
	{
		drawPreviewPiece(slot, preview[slot], pieceTypeToColor(preview[slot]));
		lv_obj_invalidate(m_nextCanvases[slot]);
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
	auto makeInfo = [&](lv_obj_t*& label, lv_color_t color)
		{
			label = lv_label_create(parent);
			lv_label_set_text(label, "");
			lv_obj_set_style_text_color(label, color, 0);
			lv_obj_set_style_text_font(label, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
			lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
		};
	makeInfo(m_attackLabel, GUI::Color::SUBTLE);
	// 攻击标签可点击，用于切换目标
	lv_obj_add_flag(m_attackLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(m_attackLabel, [](lv_event_t* e)
		{
			auto* self = static_cast<TetrisRenderer*>(lv_event_get_user_data(e));
			if (self->m_onCycleTarget) self->m_onCycleTarget();
		}, LV_EVENT_CLICKED, this);
	makeInfo(m_comboLabel, LV_COLOR_MAKE(0xFF, 0xCC, 0x00));
	makeInfo(m_garbageLabel, LV_COLOR_MAKE(0xFF, 0x44, 0x44));
}

// ============================================================
//  信息栏
// ============================================================

void TetrisRenderer::drawInfo(int combo, int garbageFlash, int attackTarget)

{
	// 攻击目标
	if (attackTarget >= 0)
	{
		char buf[24];
		snprintf(buf, sizeof(buf), "攻击: P%d", attackTarget + 1);
		lv_label_set_text(m_attackLabel, buf);
		lv_obj_clear_flag(m_attackLabel, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_label_set_text(m_attackLabel, "攻击: 下家");
		lv_obj_clear_flag(m_attackLabel, LV_OBJ_FLAG_HIDDEN);
	}

	// 连击（combo=-1 隐藏，0=首次不显示，1+=显示连击 2+）
	if (combo >= 1)
	{
		char buf[24];
		snprintf(buf, sizeof(buf), "连击x%d", combo + 1);
		lv_label_set_text(m_comboLabel, buf);
		lv_obj_clear_flag(m_comboLabel, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_add_flag(m_comboLabel, LV_OBJ_FLAG_HIDDEN);
	}

	// 垃圾行闪烁
	if (garbageFlash > 0)
	{
		char buf[24];
		snprintf(buf, sizeof(buf), "垃圾+%d", garbageFlash);
		lv_label_set_text(m_garbageLabel, buf);
		lv_obj_clear_flag(m_garbageLabel, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
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

	auto makeBtn = [&](const char* text) -> lv_obj_t*
		{
			auto btn = lv_btn_create(bar);
			lv_obj_set_size(btn, BTN_SIZE, BTN_SIZE);
			lv_obj_set_style_radius(btn, 10, 0);
			lv_obj_set_style_bg_color(btn, GUI::Color::CARD, 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);

			auto label = lv_label_create(btn);
			lv_label_set_text(label, text);
			lv_obj_center(label);
			lv_obj_set_style_text_color(label, GUI::Color::TEXT, 0);

			// 多指触摸接管：清 CLICKABLE 让 LVGL 单指针不再消费（避免双触发），
			// 触摸命中由 TouchService + TouchButton 完成
			lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
			return btn;
		};

	m_btnLeft = makeBtn("<");
	m_btnRight = makeBtn(">");
	m_btnSoft = makeBtn("v");
	m_btnHard = makeBtn("V");
	m_btnCCW = makeBtn("CCW");
	m_btnCW = makeBtn("CW");
	m_btnHold = makeBtn("H");
}

// ============================================================
//  多指触摸按钮绑定（回调跟随对象）
// ============================================================

void TetrisRenderer::bindTouch(App* owner)
{
	// 游戏按钮：Left/Right/Soft 连续输入（写 hostInputBuf），其余边沿触发（写 PlayerState）
	const auto bindBtn = [&](TouchButton& tb, lv_obj_t* btn, TouchKey key) {
		tb.bind(btn, owner, [key](bool pressed, void* user) {
			auto* r = static_cast<TetrisRenderer*>(user);
			if (!r) return;
			auto* p = r->m_playerState;
			auto* hb = r->m_hostInputBuf;
			if (!p) return;
			switch (key)
			{
			case TouchKey::Left:  if (hb) hb->left = pressed; break;
			case TouchKey::Right: if (hb) hb->right = pressed; break;
			case TouchKey::Soft:  if (hb) hb->soft = pressed; break;
			case TouchKey::Hard:  if (pressed) p->keyHard = true; break;
			case TouchKey::CCW:   if (pressed) p->keyCCW = true; break;
			case TouchKey::CW:    if (pressed) p->keyCW = true; break;
			case TouchKey::Hold:  if (pressed) p->keyHold = true; break;
			default: break;
			}
			}, this);
		};

	bindBtn(m_touchBtns[(int)TouchKey::Left], m_btnLeft, TouchKey::Left);
	bindBtn(m_touchBtns[(int)TouchKey::Right], m_btnRight, TouchKey::Right);
	bindBtn(m_touchBtns[(int)TouchKey::Soft], m_btnSoft, TouchKey::Soft);
	bindBtn(m_touchBtns[(int)TouchKey::Hard], m_btnHard, TouchKey::Hard);
	bindBtn(m_touchBtns[(int)TouchKey::CCW], m_btnCCW, TouchKey::CCW);
	bindBtn(m_touchBtns[(int)TouchKey::CW], m_btnCW, TouchKey::CW);
	bindBtn(m_touchBtns[(int)TouchKey::Hold], m_btnHold, TouchKey::Hold);

	// 退出按钮：LVGL 兼容模式（命中/松开 → LV_EVENT_CLICKED → 现有 lambda → m_onExit）
	lv_obj_clear_flag(m_btnExit, LV_OBJ_FLAG_CLICKABLE);
	m_btnExitTouch.bindLvgl(m_btnExit, owner);
}

// ============================================================
//  渲染主入口 — 对比 GameState diff 后增量更新
// ============================================================

void TetrisRenderer::syncState()

{
	if (!m_gameState) return;

	const GameState& cur = *m_gameState;

	// ── 首次渲染：强制失效所有 canvas，确保初始棋盘/预览上屏（此时布局已完成，coords 有效）──
	if (!m_initialInvalidated)
	{
		m_initialInvalidated = true;
		if (auto guard = m_display->lockGuard())
		{
			lv_obj_invalidate(m_boardCanvas);
			for (int s = 0; s < PREVIEW_COUNT; s++)
				lv_obj_invalidate(m_nextCanvases[s]);
		}
	}

	// ── 棋盘 diff（粘性 boardChanged + dirty row mask，空闲帧 0 开销）──

	if (cur.boardChanged)
	{
		const_cast<GameState&>(cur).boardChanged = false;

		// 优先使用 dirty row mask（部分行扫描），丢失时回退全量
		uint32_t dirtyMask = const_cast<Board&>(cur.board).consumeDirtyRowMask();
		if (dirtyMask == 0)
			dirtyMask = (1u << BOARD_HEIGHT) - 1;

		if (auto guard = m_display->lockGuard())
		{
			// 合并失效区域（对象本地坐标），末尾一次失效，避免失效缓冲溢出→全屏重绘
			lv_area_t dirtyArea;
			lv_area_set(&dirtyArea, LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN);
			bool hasDirty = false;

			for (int y = 0; y < BOARD_HEIGHT; y++)
			{
				if (!(dirtyMask & (1u << y))) continue;
				int lvglRow = boardYToLvglRow(y);
				for (int col = 0; col < BOARD_WIDTH; col++)
				{
					BoardCell val = cur.board.get(col, y);
					if (m_visualCache[lvglRow][col] != val)
					{
						m_visualCache[lvglRow][col] = val;
						paintBoardCell(lvglRow, col,
							val ? pieceToColor(val) : emptyColorForRow(lvglRow),
							LV_OPA_COVER);

						lv_area_t cell;
						lv_area_set(&cell, col * m_cellSize, lvglRow * m_cellSize,
							col * m_cellSize + m_cellSize - 1,
							lvglRow * m_cellSize + m_cellSize - 1);
						if (!hasDirty) { dirtyArea = cell; hasDirty = true; }
						else {
							dirtyArea.x1 = LV_MIN(dirtyArea.x1, cell.x1);
							dirtyArea.y1 = LV_MIN(dirtyArea.y1, cell.y1);
							dirtyArea.x2 = LV_MAX(dirtyArea.x2, cell.x2);
							dirtyArea.y2 = LV_MAX(dirtyArea.y2, cell.y2);
						}
					}
				}
			}
			if (hasDirty)
				invalidateBoardLocal(&dirtyArea);
		}
	}

	// ── 方块 / Ghost ──

	{
		bool pieceChanged = false, ghostChanged = false;

		pieceChanged = (cur.currentPieceType != m_prevState.currentPieceType)
			|| (cur.currentPieceX != m_prevState.currentPieceX)
			|| (cur.currentPieceY != m_prevState.currentPieceY)
			|| (cur.currentPieceRotation != m_prevState.currentPieceRotation);

		ghostChanged = (cur.ghostPieceX != m_prevState.ghostPieceX)
			|| (cur.ghostPieceY != m_prevState.ghostPieceY)
			|| (cur.currentPieceRotation != m_prevState.currentPieceRotation);

		if (pieceChanged || ghostChanged)
			if (auto guard = m_display->lockGuard())
			{
				lv_color_t color = pieceTypeToColor(cur.currentPieceType);

				// 合并失效区域（对象本地坐标），末尾一次失效
				lv_area_t dirtyArea;
				lv_area_set(&dirtyArea, LV_COORD_MAX, LV_COORD_MAX, LV_COORD_MIN, LV_COORD_MIN);
				bool hasDirty = false;
				auto addCell = [&](int r, int c) {
					if (r < 0 || r >= ROWS || c < 0 || c >= BOARD_WIDTH) return;
					lv_area_t cell;
					lv_area_set(&cell, c * m_cellSize, r * m_cellSize,
						c * m_cellSize + m_cellSize - 1, r * m_cellSize + m_cellSize - 1);
					if (!hasDirty) { dirtyArea = cell; hasDirty = true; }
					else {
						dirtyArea.x1 = LV_MIN(dirtyArea.x1, cell.x1);
						dirtyArea.y1 = LV_MIN(dirtyArea.y1, cell.y1);
						dirtyArea.x2 = LV_MAX(dirtyArea.x2, cell.x2);
						dirtyArea.y2 = LV_MAX(dirtyArea.y2, cell.y2);
					}
					};

				// 清除旧 ghost
				if (ghostChanged && m_lastGhostValid)
				{
					for (int i = 0; i < 4; i++)
					{
						int c = m_lastGhostCols[i], r = m_lastGhostRows[i];
						if (r >= 0 && r < ROWS)
						{
							paintBoardCell(r, c,
								m_visualCache[r][c]
								? pieceToColor(m_visualCache[r][c])
								: emptyColorForRow(r), LV_OPA_COVER);
							addCell(r, c);
						}
					}
					m_lastGhostValid = false;
				}

				// 清除旧方块
				if (pieceChanged && m_lastPieceValid)
				{
					for (int i = 0; i < 4; i++)
					{
						int c = m_lastPieceCols[i], r = m_lastPieceRows[i];
						if (r >= 0 && r < ROWS)
						{
							paintBoardCell(r, c,
								m_visualCache[r][c]
								? pieceToColor(m_visualCache[r][c])
								: emptyColorForRow(r), LV_OPA_COVER);
							addCell(r, c);
						}
					}
					m_lastPieceValid = false;
				}

				// 画新 ghost
				if (ghostChanged && cur.currentPieceType != PieceType::NONE)
				{
					// 从 currentPiece 信息重建 ghost
					Piece ghost(cur.currentPieceType, cur.currentPieceRotation,
						cur.currentPieceX, cur.currentPieceY);
					int gy = cur.ghostPieceY;
					ghost.setY(gy);
					int cols[4], rows[4];
					ghost.getBlocks(cols, rows);
					for (int i = 0; i < 4; i++)
					{
						int r = boardYToLvglRow(rows[i]);
						m_lastGhostCols[i] = cols[i];
						m_lastGhostRows[i] = r;
						if (r >= 0 && r < ROWS)
						{
							paintBoardCell(r, cols[i], color, LV_OPA_30);
							addCell(r, cols[i]);
						}
					}
					m_lastGhostValid = true;
				}

				// 画新方块
				if (pieceChanged && cur.currentPieceType != PieceType::NONE)
				{
					// 从 currentPiece 信息重建
					Piece p(cur.currentPieceType, cur.currentPieceRotation,
						cur.currentPieceX, cur.currentPieceY);
					int cols[4], rows[4];
					p.getBlocks(cols, rows);
					for (int i = 0; i < 4; i++)
					{
						int r = boardYToLvglRow(rows[i]);
						m_lastPieceCols[i] = cols[i];
						m_lastPieceRows[i] = r;
						if (r >= 0 && r < ROWS)
						{
							paintBoardCell(r, cols[i], color, LV_OPA_COVER);
							addCell(r, cols[i]);
						}
					}
					m_lastPieceValid = true;
				}

				if (hasDirty)
					invalidateBoardLocal(&dirtyArea);
			}
	}

	// ── 预览队列 ──

	{
		bool previewChanged = false;
		for (int s = 0; s < 4; s++)
		{
			if (cur.nextPieces[s] != m_prevState.nextPieces[s])
			{
				previewChanged = true;
				break;
			}
		}
		if (previewChanged)
			if (auto guard = m_display->lockGuard())
			{
				drawNext(cur.nextPieces);
			}
	}

	// ── Hold ──

	{
		if (cur.holdPiece != m_prevState.holdPiece
			|| cur.holdUsed != m_prevState.holdUsed)
		{
			// Hold 显示已合并到预览区第一个槽
			PieceType preview[4];
			for (int s = 0; s < 4; s++) preview[s] = cur.nextPieces[s];
			if (auto guard = m_display->lockGuard())
				drawNext(preview);
		}
	}

	// ── 分数 / 信息 ──

	{
		if (cur.score != m_prevState.score
			|| cur.combo != m_prevState.combo
			|| cur.garbageFlash != m_prevState.garbageFlash
			|| cur.attackTarget != m_prevState.attackTarget)
			if (auto guard = m_display->lockGuard())
			{
				drawInfo(cur.combo, cur.garbageFlash, cur.attackTarget);
			}
	}

	// ── GAME OVER ──
	if (cur.gameOver && !m_prevState.gameOver)
		if (auto guard = m_display->lockGuard())
		{
			lv_obj_clear_flag(m_gameOverOverlay, LV_OBJ_FLAG_HIDDEN);
			lv_obj_clear_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
		}

	// ── 保存快照供下次 diff ──
	m_prevState = cur;
}
