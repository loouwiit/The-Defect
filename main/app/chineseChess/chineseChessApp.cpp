#include "chineseChessApp.hpp"
#include "display/font.hpp"
#include "display/display.hpp"
#include "app/appStackManager.hpp"
#include "gui/gui.hpp"
#include "esp_log.h"
#include "lvgl.h"

#include <cstdlib>
#include <ctime>
#include <cmath>

// ============================================================
// 颜色常量
// ============================================================
namespace
{
	constexpr int CELL_SZ = 64;
	constexpr int PIECE_SZ = CELL_SZ - 4;
	constexpr int PIECE_RAD = PIECE_SZ / 2;

	constexpr uint32_t BG = 0xff0a0a1e;
	constexpr uint32_t GRID_LINE = 0xff3a3a55;
	constexpr uint32_t RIVER_TEXT = 0xff555577;

	constexpr uint32_t PIECE_RED_BG = 0xffd4a574;
	constexpr uint32_t PIECE_RED_TEXT = 0xffcc0000;
	constexpr uint32_t PIECE_BLACK_BG = 0xff2d2d2d;
	constexpr uint32_t PIECE_BLACK_TEXT = 0xff000000;

	constexpr uint32_t SELECTION_HIGHLIGHT = 0x44ffdd00;
	constexpr uint32_t CURSOR_HIGHLIGHT = 0x44ffffff;
	constexpr uint32_t MOVE_MARKER = 0x6600ff88;
	constexpr uint32_t CAPTURE_MARKER = 0x66ff5252;

	constexpr uint32_t TEXT_COLOR = 0xffffffff;
	constexpr uint32_t SUBTLE = 0xff888899;
	constexpr uint32_t TURN_RED = 0xffcc0000;
	constexpr uint32_t TURN_BLACK = 0xff888888;
	constexpr uint32_t BTN_RESTART = 0xff00c853;
	constexpr uint32_t BTN_BACK = 0xff555566;
}

// ============================================================
// ChineseChessApp
// ============================================================

ChineseChessApp::ChineseChessApp(Display* display, int opponentType)
	: App(display)
	, m_opponentType{ opponentType }
{
}

ChineseChessApp::~ChineseChessApp() = default;

void ChineseChessApp::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法锁定显示");
		return;
	}

	// 背景
	lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// 创建所有 LVGL 对象
	createBoard(screen);
	createPieces(screen);
	createUI(screen);

	// 初始化棋盘
	m_logic.reset();
	m_dirty = true;

	// 同步初始状态到视觉缓存并刷新
	syncBoard();
	updateSelection();
	updateLabels();

	// 注册屏幕触摸回调
	lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_event_cb(screen, onScreenClickCb, LV_EVENT_CLICKED, this);

	ESP_LOGI(TAG, "中国象棋启动, opponentType=%d, board at (%d,%d) cell=%d, screen %dx%d",
		m_opponentType, OFFSET_X, OFFSET_Y, CELL_SIZE, BOARD_W, BOARD_H);

	// 启动游戏线程
	running = true;
	m_thread = Thread{ gameLoop, "chessGame", this, Thread::Priority::Normal, 4096 };
}

void ChineseChessApp::deinit()
{
	running = false;
	vTaskDelay(pdMS_TO_TICKS(200));
	m_thread = {};

	// LVGL 对象由 screen 析构时自动回收
	deletable = true;
}

void ChineseChessApp::onForeground()
{
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	m_focusIdx = 0;
	m_dirty = true;
}

// ============================================================
// 创建棋盘背景
// ============================================================

void ChineseChessApp::createBoard(lv_obj_t* parent)
{
	int boardPxW = 8 * CELL_SIZE;
	int boardPxH = 9 * CELL_SIZE;

	// ── 9 条竖线 ──
	for (int x = 0; x < 9; x++)
	{
		m_gridLinesV[x] = lv_obj_create(parent);
		lv_obj_set_size(m_gridLinesV[x], 1, boardPxH);
		lv_obj_set_pos(m_gridLinesV[x], OFFSET_X + x * CELL_SIZE, OFFSET_Y);
		lv_obj_set_style_bg_color(m_gridLinesV[x], lv_color_hex(GRID_LINE), 0);
		lv_obj_set_style_bg_opa(m_gridLinesV[x], LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(m_gridLinesV[x], 0, 0);
		lv_obj_remove_flag(m_gridLinesV[x], LV_OBJ_FLAG_CLICKABLE);
	}

	// ── 10 条横线 ──
	for (int y = 0; y < 10; y++)
	{
		m_gridLinesH[y] = lv_obj_create(parent);
		// 边界线贯穿整个棋盘，中间三条（y=0,9）也贯穿，但 y=1~8 只画到左右边界
		lv_obj_set_size(m_gridLinesH[y], boardPxW, 1);
		lv_obj_set_pos(m_gridLinesH[y], OFFSET_X, OFFSET_Y + y * CELL_SIZE);
		lv_obj_set_style_bg_color(m_gridLinesH[y], lv_color_hex(GRID_LINE), 0);
		lv_obj_set_style_bg_opa(m_gridLinesH[y], LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(m_gridLinesH[y], 0, 0);
		lv_obj_remove_flag(m_gridLinesH[y], LV_OBJ_FLAG_CLICKABLE);
	}

	// ── 士/象位置的斜线（九宫格） ──
	auto mkDiag = [&](int x1, int y1, int x2, int y2) {
		// 用矩形旋转实现斜线
		lv_obj_t* line = lv_obj_create(parent);
		int dx = (x2 - x1) * CELL_SIZE;
		int dy = (y2 - y1) * CELL_SIZE;
		int len = sqrt(dx * dx + dy * dy);
		lv_obj_set_size(line, len, 1);
		lv_obj_set_pos(line, OFFSET_X + x1 * CELL_SIZE, OFFSET_Y + y1 * CELL_SIZE);
		float angle = atan2f(dy, dx) * 180.0f / 3.14159f;
		lv_obj_set_style_transform_rotation(line, angle * 10, 0); // LVGL: 0.1°
		lv_obj_set_style_transform_pivot_x(line, 0, 0);
		lv_obj_set_style_transform_pivot_y(line, 0, 0);
		lv_obj_set_style_bg_color(line, lv_color_hex(GRID_LINE), 0);
		lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(line, 0, 0);
		lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
	};

	// 上方九宫（黑方）
	mkDiag(3, 0, 5, 2);
	mkDiag(5, 0, 3, 2);

	// 下方九宫（红方）
	mkDiag(3, 7, 5, 9);
	mkDiag(5, 7, 3, 9);

	// ── 楚河漢界 ──
	m_riverLabel = lv_label_create(parent);
	lv_label_set_text(m_riverLabel, "楚河        漢界");
	lv_obj_set_style_text_color(m_riverLabel, lv_color_hex(RIVER_TEXT), 0);
	lv_obj_set_style_text_font(m_riverLabel, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_set_pos(m_riverLabel, OFFSET_X, OFFSET_Y + 4 * CELL_SIZE + 8);
	lv_obj_set_width(m_riverLabel, 8 * CELL_SIZE);
	lv_obj_set_style_text_align(m_riverLabel, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_remove_flag(m_riverLabel, LV_OBJ_FLAG_CLICKABLE);
}

// ============================================================
// 创建棋子对象池
// ============================================================

void ChineseChessApp::createPieces(lv_obj_t* parent)
{
	for (int x = 0; x < 9; x++)
	{
		for (int y = 0; y < 10; y++)
		{
			// 圆形背景
			m_pieceBg[x][y] = lv_obj_create(parent);
	lv_obj_set_size(m_pieceBg[x][y], PIECE_SZ, PIECE_SZ);
		lv_obj_set_style_radius(m_pieceBg[x][y], PIECE_RAD, 0);
			lv_obj_set_style_border_width(m_pieceBg[x][y], 1, 0);
			lv_obj_set_style_border_color(m_pieceBg[x][y], lv_color_hex(0xff555555), 0);
			lv_obj_set_style_border_opa(m_pieceBg[x][y], LV_OPA_30, 0);
			lv_obj_set_style_shadow_width(m_pieceBg[x][y], 6, 0);
			lv_obj_set_style_shadow_color(m_pieceBg[x][y], lv_color_hex(0x000000), 0);
			lv_obj_set_style_shadow_opa(m_pieceBg[x][y], LV_OPA_50, 0);
			lv_obj_remove_flag(m_pieceBg[x][y], LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(m_pieceBg[x][y], LV_OBJ_FLAG_HIDDEN);

			// 棋子文字（作为圆形背景的子对象，自动居中）
		m_pieceText[x][y] = lv_label_create(m_pieceBg[x][y]);
		lv_obj_set_style_text_font(m_pieceText[x][y],
			FontLoader::getDefault(FontLoader::FontSize::Medium), 0);
		lv_obj_center(m_pieceText[x][y]);
			lv_obj_add_flag(m_pieceText[x][y], LV_OBJ_FLAG_HIDDEN);
			lv_obj_remove_flag(m_pieceText[x][y], LV_OBJ_FLAG_CLICKABLE);
		}
	}

	// 选中高亮（黄色边框）
	m_selectionHighlight = lv_obj_create(parent);
	lv_obj_set_size(m_selectionHighlight, CELL_SIZE, CELL_SIZE);
	lv_obj_set_style_radius(m_selectionHighlight, 4, 0);
	lv_obj_set_style_border_width(m_selectionHighlight, 3, 0);
	lv_obj_set_style_border_color(m_selectionHighlight, lv_color_hex(SELECTION_HIGHLIGHT), 0);
	lv_obj_set_style_border_opa(m_selectionHighlight, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_opa(m_selectionHighlight, LV_OPA_TRANSP, 0);
	lv_obj_remove_flag(m_selectionHighlight, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_selectionHighlight, LV_OBJ_FLAG_HIDDEN);

	// 手柄光标（白色半透明边框）
	m_cursorHighlight = lv_obj_create(parent);
	lv_obj_set_size(m_cursorHighlight, CELL_SIZE, CELL_SIZE);
	lv_obj_set_style_radius(m_cursorHighlight, 4, 0);
	lv_obj_set_style_border_width(m_cursorHighlight, 2, 0);
	lv_obj_set_style_border_color(m_cursorHighlight, lv_color_hex(CURSOR_HIGHLIGHT), 0);
	lv_obj_set_style_border_opa(m_cursorHighlight, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_opa(m_cursorHighlight, LV_OPA_TRANSP, 0);
	lv_obj_remove_flag(m_cursorHighlight, LV_OBJ_FLAG_CLICKABLE);

	// 合法走法标记（绿色小圆点 / 红色捕获标记）
	for (int i = 0; i < 30; i++)
	{
		m_moveMarkers[i] = lv_obj_create(parent);
		lv_obj_set_size(m_moveMarkers[i], 14, 14);
		lv_obj_set_style_radius(m_moveMarkers[i], 7, 0);
		lv_obj_set_style_border_width(m_moveMarkers[i], 0, 0);
		lv_obj_set_style_bg_opa(m_moveMarkers[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(m_moveMarkers[i], lv_color_hex(MOVE_MARKER), 0);
		lv_obj_remove_flag(m_moveMarkers[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_moveMarkers[i], LV_OBJ_FLAG_HIDDEN);
	}
}

// ============================================================
// 创建 UI 标签
// ============================================================

void ChineseChessApp::createUI(lv_obj_t* parent)
{
	// ── 回合指示（顶部居中） ──
	m_turnLabel = lv_label_create(parent);
	lv_label_set_text(m_turnLabel, "红方走棋");
	lv_obj_set_style_text_color(m_turnLabel, lv_color_hex(TEXT_COLOR), 0);
	lv_obj_set_style_text_font(m_turnLabel, FontLoader::getDefault(FontLoader::FontSize::Medium), 0);
	lv_obj_align(m_turnLabel, LV_ALIGN_TOP_MID, 0, 16);

	// ── 状态提示（回合下方） ──
	m_statusLabel = lv_label_create(parent);
	lv_label_set_text(m_statusLabel, "");
	lv_obj_set_style_text_color(m_statusLabel, lv_color_hex(SUBTLE), 0);
	lv_obj_set_style_text_font(m_statusLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(m_statusLabel, LV_ALIGN_TOP_MID, 0, 55);

	// ── 游戏结束标签 ──
	m_gameOverLabel = lv_label_create(parent);
	lv_label_set_text(m_gameOverLabel, "游戏结束");
	lv_obj_set_style_text_color(m_gameOverLabel, lv_color_hex(0xffff5252), 0);
	lv_obj_set_style_text_font(m_gameOverLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(m_gameOverLabel, LV_ALIGN_CENTER, 0, -100);
	lv_obj_add_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);

	// ── 游戏结束得分 ──
	m_gameOverScoreLabel = lv_label_create(parent);
	lv_label_set_text(m_gameOverScoreLabel, "");
	lv_obj_set_style_text_color(m_gameOverScoreLabel, lv_color_hex(0xffff5252), 0);
	lv_obj_set_style_text_font(m_gameOverScoreLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(m_gameOverScoreLabel, LV_ALIGN_CENTER, 0, -30);
	lv_obj_add_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);

	// ── 重新开始按钮 ──
	m_restartBtn = lv_button_create(parent);
	lv_obj_set_size(m_restartBtn, 200, 60);
	lv_obj_set_style_radius(m_restartBtn, 16, 0);
	lv_obj_set_style_bg_color(m_restartBtn, lv_color_hex(BTN_RESTART), 0);
	lv_obj_set_style_bg_opa(m_restartBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_restartBtn, 12, 0);
	lv_obj_set_style_shadow_color(m_restartBtn, lv_color_hex(BTN_RESTART), 0);
	lv_obj_set_style_shadow_opa(m_restartBtn, LV_OPA_40, 0);
	lv_obj_set_style_border_width(m_restartBtn, 0, 0);
	lv_obj_set_style_outline_width(m_restartBtn, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_restartBtn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(m_restartBtn, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_restartBtn, 3, LV_STATE_FOCUSED);
	lv_obj_align(m_restartBtn, LV_ALIGN_CENTER, 0, 50);
	auto lblRestart = lv_label_create(m_restartBtn);
	lv_label_set_text(lblRestart, "重新开始");
	lv_obj_set_style_text_color(lblRestart, lv_color_hex(0x000000), 0);
	lv_obj_center(lblRestart);
	lv_obj_add_event_cb(m_restartBtn, btnRestartCb, LV_EVENT_CLICKED, this);
	lv_obj_add_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);

	// ── 返回菜单按钮 ──
	m_backBtn = lv_button_create(parent);
	lv_obj_set_size(m_backBtn, 200, 60);
	lv_obj_set_style_radius(m_backBtn, 16, 0);
	lv_obj_set_style_bg_color(m_backBtn, lv_color_hex(BTN_BACK), 0);
	lv_obj_set_style_bg_opa(m_backBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_backBtn, 12, 0);
	lv_obj_set_style_shadow_color(m_backBtn, lv_color_hex(BTN_BACK), 0);
	lv_obj_set_style_shadow_opa(m_backBtn, LV_OPA_40, 0);
	lv_obj_set_style_border_width(m_backBtn, 0, 0);
	lv_obj_set_style_outline_width(m_backBtn, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_backBtn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(m_backBtn, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_align(m_backBtn, LV_ALIGN_CENTER, 0, 130);
	auto lblBack = lv_label_create(m_backBtn);
	lv_label_set_text(lblBack, "返回菜单");
	lv_obj_set_style_text_color(lblBack, lv_color_hex(TEXT_COLOR), 0);
	lv_obj_center(lblBack);
	lv_obj_add_event_cb(m_backBtn, btnBackCb, LV_EVENT_CLICKED, this);
	lv_obj_add_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// 坐标转换
// ============================================================

int ChineseChessApp::coordToGrid(int pixel, int offset, int maxGrid) const
{
	int g = (pixel - offset + CELL_SIZE / 2) / CELL_SIZE;
	if (g < 0) g = 0;
	if (g > maxGrid) g = maxGrid;
	return g;
}

int ChineseChessApp::gridToCoord(int grid, int offset) const
{
	return offset + grid * CELL_SIZE;
}

// ============================================================
// 棋盘同步（增量更新）
// ============================================================

void ChineseChessApp::syncBoard()
{
	for (int x = 0; x < 9; x++)
		for (int y = 0; y < 10; y++)
			syncCell(x, y);
}

void ChineseChessApp::syncCell(int x, int y)
{
	const auto& piece = m_logic.getPiece(x, y);
	auto& cache = m_visualCache[x][y];

	// 检测是否变化
	if (cache.side == piece.side && cache.type == piece.type && cache.alive == piece.alive)
		return;

	cache.side = piece.side;
	cache.type = piece.type;
	cache.alive = piece.alive;

	auto* bg = m_pieceBg[x][y];
	auto* txt = m_pieceText[x][y];

	if (piece.isEmpty() || !piece.alive)
	{
		lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(txt, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// 显示棋子（中心对齐到格线交点）
	int px = OFFSET_X + x * CELL_SIZE - PIECE_SZ / 2;
	int py = OFFSET_Y + y * CELL_SIZE - PIECE_SZ / 2;

	lv_obj_set_pos(bg, px, py);

	// 颜色
	if (piece.side == ChessLogic::Side::Red)
	{
		lv_obj_set_style_bg_color(bg, lv_color_hex(PIECE_RED_BG), 0);
		lv_obj_set_style_text_color(txt, lv_color_hex(PIECE_RED_TEXT), 0);
	}
	else
	{
		lv_obj_set_style_bg_color(bg, lv_color_hex(PIECE_BLACK_BG), 0);
		lv_obj_set_style_text_color(txt, lv_color_hex(PIECE_BLACK_TEXT), 0);
	}

	lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
	lv_label_set_text(txt, ChessLogic::pieceChar(piece.type, piece.side));

	lv_obj_clear_flag(bg, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(txt, LV_OBJ_FLAG_HIDDEN);
}

void ChineseChessApp::updateSelection()
{
	// ── 选中高亮（中心对齐到交点） ──
	if (m_selected && m_selX >= 0 && m_selY >= 0)
	{
		lv_obj_set_pos(m_selectionHighlight,
			OFFSET_X + m_selX * CELL_SIZE - CELL_SIZE / 2,
			OFFSET_Y + m_selY * CELL_SIZE - CELL_SIZE / 2);
		lv_obj_clear_flag(m_selectionHighlight, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_add_flag(m_selectionHighlight, LV_OBJ_FLAG_HIDDEN);
	}

	// ── 手柄光标（中心对齐到交点） ──
	lv_obj_set_pos(m_cursorHighlight,
		OFFSET_X + m_cursorX * CELL_SIZE - CELL_SIZE / 2,
		OFFSET_Y + m_cursorY * CELL_SIZE - CELL_SIZE / 2);

	// ── 合法走法标记 ──
	int markerIdx = 0;
	for (const auto& mv : m_validMoves)
	{
		if (markerIdx >= 30) break;

		bool isCapture = !m_logic.getPiece(mv.x, mv.y).isEmpty();
		int cx = OFFSET_X + mv.x * CELL_SIZE - 7;
		int cy = OFFSET_Y + mv.y * CELL_SIZE - 7;

		lv_obj_set_pos(m_moveMarkers[markerIdx], cx, cy);
		lv_obj_set_style_bg_color(m_moveMarkers[markerIdx],
			isCapture ? lv_color_hex(CAPTURE_MARKER) : lv_color_hex(MOVE_MARKER), 0);
		lv_obj_clear_flag(m_moveMarkers[markerIdx], LV_OBJ_FLAG_HIDDEN);
		markerIdx++;
	}

	// 隐藏多余的标记
	for (; markerIdx < 30; markerIdx++)
		lv_obj_add_flag(m_moveMarkers[markerIdx], LV_OBJ_FLAG_HIDDEN);
}

void ChineseChessApp::updateLabels()
{
	if (m_logic.getState() == ChessLogic::State::GameOver)
	{
		lv_label_set_text(m_turnLabel, "");
		lv_label_set_text(m_statusLabel, "");
		return;
	}

	bool isRedTurn = (m_logic.getCurrentTurn() == ChessLogic::Side::Red);
	lv_label_set_text(m_turnLabel, isRedTurn ? "红方走棋" : "黑方走棋");
	lv_obj_set_style_text_color(m_turnLabel,
		isRedTurn ? lv_color_hex(TURN_RED) : lv_color_hex(TURN_BLACK), 0);

	// 将军提示
	if (m_logic.isInCheck(m_logic.getCurrentTurn()))
	{
		lv_label_set_text(m_statusLabel, "将军！");
		lv_obj_set_style_text_color(m_statusLabel, lv_color_hex(0xffff5252), 0);
	}
	else
	{
		lv_label_set_text(m_statusLabel, "");
	}
}

// ============================================================
// GameOver UI
// ============================================================

void ChineseChessApp::showGameOver()
{
	auto winner = m_logic.getWinner();
	if (winner == ChessLogic::Side::Red)
		lv_label_set_text(m_gameOverScoreLabel, "红方胜利！");
	else if (winner == ChessLogic::Side::Black)
		lv_label_set_text(m_gameOverScoreLabel, "黑方胜利！");
	else
		lv_label_set_text(m_gameOverScoreLabel, "平局！");

	lv_obj_clear_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);
	applyFocus();
}

void ChineseChessApp::hideGameOver()
{
	lv_obj_add_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// 输入处理 — 触摸
// ============================================================

void ChineseChessApp::onTouchClick(int px, int py)
{
	if (m_logic.getState() != ChessLogic::State::Playing)
		return;

	// 棋盘触摸范围（比实际棋盘每边多半格，便于点击边缘棋子）
	int boardLeft = OFFSET_X - CELL_SIZE / 2;
	int boardRight = OFFSET_X + 8 * CELL_SIZE + CELL_SIZE / 2;
	int boardTop = OFFSET_Y - CELL_SIZE / 2;
	int boardBottom = OFFSET_Y + 9 * CELL_SIZE + CELL_SIZE / 2;
	if (px < boardLeft || px > boardRight || py < boardTop || py > boardBottom)
		return;

	// 转换到网格坐标（x:0~8, y:0~9）
	int gx = coordToGrid(px, OFFSET_X, 8);
	int gy = coordToGrid(py, OFFSET_Y, 9);

	const auto& target = m_logic.getPiece(gx, gy);

	if (m_selected)
	{
		// 已选中：检查是否点击了合法走法
		bool isValidTarget = false;
		for (const auto& mv : m_validMoves)
		{
			if (mv.x == gx && mv.y == gy)
			{
				isValidTarget = true;
				break;
			}
		}

		if (isValidTarget)
		{
			// ⾛棋
			m_logic.movePiece(m_selX, m_selY, gx, gy);
			m_selected = false;
			m_validMoves.clear();
			m_dirty = true;
		}
		else if (!target.isEmpty() && target.side == m_logic.getCurrentTurn())
		{
			// 点击己方其他棋子 → 切换选中
			m_selX = gx;
			m_selY = gy;
			m_selected = true;
			m_validMoves = m_logic.getValidMoves(gx, gy);
			m_dirty = true;
		}
		else
		{
			// 点击空位或对方棋子（非合法走法） → 取消选中
			m_selected = false;
			m_validMoves.clear();
			m_dirty = true;
		}
	}
	else
	{
		// 未选中：点击己方棋子
		if (!target.isEmpty() && target.side == m_logic.getCurrentTurn())
		{
			m_selX = gx;
			m_selY = gy;
			m_selected = true;
			m_validMoves = m_logic.getValidMoves(gx, gy);
			m_dirty = true;
		}
	}
}

// ============================================================
// 输入处理 — 手柄 select/cancel
// ============================================================

void ChineseChessApp::handleSelect()
{
	if (m_logic.getState() == ChessLogic::State::GameOver)
	{
		activateFocus();
		return;
	}

	const auto& target = m_logic.getPiece(m_cursorX, m_cursorY);

	if (m_selected)
	{
		// 检查光标位置是否在合法走法中
		for (const auto& mv : m_validMoves)
		{
			if (mv.x == m_cursorX && mv.y == m_cursorY)
			{
				m_logic.movePiece(m_selX, m_selY, m_cursorX, m_cursorY);
				m_selected = false;
				m_validMoves.clear();
				m_dirty = true;
				return;
			}
		}

		// 光标位置不是合法走法，取消选中
		m_selected = false;
		m_validMoves.clear();
		m_dirty = true;

		// 如果光标在自己棋子上，重新选中
		if (!target.isEmpty() && target.side == m_logic.getCurrentTurn())
		{
			m_selX = m_cursorX;
			m_selY = m_cursorY;
			m_selected = true;
			m_validMoves = m_logic.getValidMoves(m_cursorX, m_cursorY);
			m_dirty = true;
		}
	}
	else
	{
		// 选中光标位置的棋子（如果是己方）
		if (!target.isEmpty() && target.side == m_logic.getCurrentTurn())
		{
			m_selX = m_cursorX;
			m_selY = m_cursorY;
			m_selected = true;
			m_validMoves = m_logic.getValidMoves(m_cursorX, m_cursorY);
			m_dirty = true;
		}
	}
}

void ChineseChessApp::handleCancel()
{
	if (m_selected)
	{
		// 取消选中
		m_selected = false;
		m_validMoves.clear();
		m_dirty = true;
	}
}

// ============================================================
// 焦点导航（GameOver 按钮）
// ============================================================

void ChineseChessApp::applyFocus()
{
	auto clear = [](lv_obj_t* obj) { if (obj) lv_obj_clear_state(obj, LV_STATE_FOCUSED); };
	auto focus = [](lv_obj_t* obj) { if (obj) lv_obj_add_state(obj, LV_STATE_FOCUSED); };

	clear(m_restartBtn);
	clear(m_backBtn);

	switch (m_focusIdx)
	{
	case 0: focus(m_restartBtn); break;
	case 1: focus(m_backBtn);    break;
	}
}

void ChineseChessApp::activateFocus()
{
	switch (m_focusIdx)
	{
	case 0: lv_obj_send_event(m_restartBtn, LV_EVENT_CLICKED, nullptr); break;
	case 1: lv_obj_send_event(m_backBtn,   LV_EVENT_CLICKED, nullptr); break;
	}
}

// ============================================================
// 手柄输入
// ============================================================

void ChineseChessApp::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// 边沿检测
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	auto gameState = m_logic.getState();

	// ── B 键：取消/返回 ──
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (gameState == ChessLogic::State::GameOver)
		{
			auto guard = display->lockGuard();
			Task::addTask([](void* p) -> TickType_t {
				static_cast<ChineseChessApp*>(p)->popApp();
				return Task::infinityTime;
				}, "chessBack", this, 0, Task::Affinity::None);
			return;
		}
		else if (m_selected)
		{
			auto guard = display->lockGuard();
			handleCancel();
		}
		return;
	}

	// ── A 键 / L3：选中/确认 ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			auto guard = display->lockGuard();
			handleSelect();
		}
		return;
	}

	// ── 摇杆归位判断 ──
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[playerId] = 0;
		return;
	}
	if (m_nextMoveTime[playerId] >= xTaskGetTickCount())
		return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	if (gameState == ChessLogic::State::Playing)
	{
		// 光标移动
		int newX = m_cursorX;
		int newY = m_cursorY;
		if (lxLeft && newX > 0)  newX--;
		if (lxRight && newX < 8) newX++;
		if (lyUp && newY > 0)    newY--;
		if (lyDown && newY < 9)  newY++;

		if (newX != m_cursorX || newY != m_cursorY)
		{
			m_cursorX = newX;
			m_cursorY = newY;
			auto guard = display->lockGuard();
			updateSelection();
		}
	}
	else if (gameState == ChessLogic::State::GameOver)
	{
		// 上下导航 GameOver 按钮
		if (lyUp && m_focusIdx > 0)   m_focusIdx--;
		if (lyDown && m_focusIdx < 1) m_focusIdx++;
		auto guard = display->lockGuard();
		applyFocus();
	}
}

// ============================================================
// LVGL 回调
// ============================================================

void ChineseChessApp::onScreenClickCb(lv_event_t* e)
{
	auto self = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
	auto* indev = lv_event_get_indev(e);
	if (!indev) return;
	lv_point_t pt;
	lv_indev_get_point(indev, &pt);
	self->onTouchClick(pt.x, pt.y);
}

void ChineseChessApp::btnRestartCb(lv_event_t* e)
{
	auto self = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
	Task::addTask([](void* p) -> TickType_t {
		auto* game = static_cast<ChineseChessApp*>(p);
		game->replaceWith(new ChineseChessApp(game->display, game->m_opponentType));
		return Task::infinityTime;
		}, "restartChess", self, 0, Task::Affinity::None);
}

void ChineseChessApp::btnBackCb(lv_event_t* e)
{
	auto self = static_cast<ChineseChessApp*>(lv_event_get_user_data(e));
	Task::addTask([](void* p) -> TickType_t {
		static_cast<ChineseChessApp*>(p)->popApp();
		return Task::infinityTime;
		}, "backChess", self, 0, Task::Affinity::None);
}

// ============================================================
// 游戏主循环
// ============================================================

void ChineseChessApp::gameLoop(void* param)
{
	auto& self = *static_cast<ChineseChessApp*>(param);
	ESP_LOGI(TAG, "游戏循环启动");

	while (self.running)
	{
		bool needsUpdate = false;

		// ── AI 回合 ──
		if (self.m_opponentType == 0 &&
			self.m_logic.getState() == ChessLogic::State::Playing &&
			self.m_logic.getCurrentTurn() == ChessLogic::Side::Black)
		{
			vTaskDelay(pdMS_TO_TICKS(400)); // AI "思考" 延迟

			auto move = self.m_ai.getMove(self.m_logic);
			if (move.from.valid() && move.to.valid())
			{
				self.m_logic.movePiece(move.from.x, move.from.y, move.to.x, move.to.y);
				self.m_selected = false;
				self.m_validMoves.clear();
				needsUpdate = true;
			}
		}

		// ── 脏标记触发 ──
		if (self.m_dirty || needsUpdate)
		{
			auto guard = self.display->lockGuard();
			if (guard)
			{
				self.syncBoard();
				self.updateSelection();
				self.updateLabels();

				if (self.m_logic.getState() == ChessLogic::State::GameOver)
					self.showGameOver();
				else
					self.hideGameOver();
			}
			self.m_dirty = false;
		}

		vTaskDelay(pdMS_TO_TICKS(30)); // ~33fps
	}

	ESP_LOGI(TAG, "游戏循环退出");
	self.deletable = true;

	while (true)
		vTaskDelay(pdMS_TO_TICKS(5000));
}
