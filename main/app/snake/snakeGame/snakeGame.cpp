#include "snakeGame.hpp"
#include "display/font.hpp"
// TODO: 远程导航 — 后续参考 tetris 分支的 WS 架构重新实现
// #include "wsServer/wsServer.hpp"
#include "esp_log.h"
#include "lvgl.h"

#include <cstring>

// ============================================================
// 颜色常量
// ============================================================
namespace Color
{
	constexpr uint32_t BG_TOP = 0xff0a0a1e;

	constexpr uint32_t SNAKE1 = 0xff00e676;
	constexpr uint32_t SNAKE1_GLOW = 0x2200e676;

	constexpr uint32_t SNAKE2 = 0xff448aff;
	constexpr uint32_t SNAKE2_GLOW = 0x22448aff;

	constexpr uint32_t SNAKE3 = 0xffffa726;
	constexpr uint32_t SNAKE3_GLOW = 0x22ffa726;

	constexpr uint32_t SNAKE4 = 0xffce93d8;  // P4 蛇身（紫色）
	constexpr uint32_t SNAKE4_GLOW = 0x22ce93d8; // P4 发光

	constexpr uint32_t GRID_LINE = 0xff1e1e35;

	constexpr uint32_t FOOD = 0xffff5252;
	constexpr uint32_t FOOD_GLOW = 0x44ff5252;

	constexpr uint32_t DPAD_BG = 0x221a1a2e;
	constexpr uint32_t DPAD_BTN = 0x55333355;
	constexpr uint32_t DPAD_ARROW = 0xccffffff;

	constexpr uint32_t TEXT = 0xffffffff;
	constexpr uint32_t SUBTLE = 0xff888899;
	constexpr uint32_t BTN_1P = 0xff00c853;
}

// ============================================================
// SnakeGame
// ============================================================

SnakeGame::SnakeGame(Display* display, int playerCount)
	: App(display)
	, m_logic{ playerCount }
	, m_playerCount{ playerCount }
{
}

SnakeGame::~SnakeGame() = default;

void SnakeGame::init()
{
	App::init();

	auto guard = display->lockGuard();
	if (!guard)
	{
		ESP_LOGE(TAG, "无法锁定显示");
		return;
	}

	// 背景
	lv_obj_set_style_bg_color(screen, lv_color_hex(Color::BG_TOP), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);

	// 创建游戏对象池 + D-pad
	createObjectPool(screen);
	createDpad(screen);

	// 根据玩家数显示对应 D-pad
	lv_obj_add_flag(m_pad2, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_pad4, LV_OBJ_FLAG_HIDDEN);
	if (m_playerCount >= 2) lv_obj_clear_flag(m_pad2, LV_OBJ_FLAG_HIDDEN);
	if (m_playerCount >= 3) lv_obj_clear_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);
	if (m_playerCount >= 4) lv_obj_clear_flag(m_pad4, LV_OBJ_FLAG_HIDDEN);

	// 分数标签（初始隐藏）
	m_scoreLabel = lv_label_create(screen);
	lv_label_set_text(m_scoreLabel, "");
	lv_obj_set_style_text_color(m_scoreLabel, lv_color_hex(Color::TEXT), 0);
	lv_obj_set_style_text_font(m_scoreLabel, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(m_scoreLabel, LV_ALIGN_TOP_MID, 0, 10);
	lv_obj_add_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);

	// 状态标签（初始隐藏�?
	m_statusLabel = lv_label_create(screen);
	lv_label_set_text(m_statusLabel, "");
	lv_obj_set_style_text_color(m_statusLabel, lv_color_hex(Color::SUBTLE), 0);
	lv_obj_set_style_text_font(m_statusLabel, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_align(m_statusLabel, LV_ALIGN_TOP_MID, 0, 35);
	lv_obj_add_flag(m_statusLabel, LV_OBJ_FLAG_HIDDEN);

	// 游戏结束标签（初始隐藏）
	m_gameOverLabel = lv_label_create(screen);
	lv_label_set_text(m_gameOverLabel, "游戏结束");
	lv_obj_set_style_text_color(m_gameOverLabel, lv_color_hex(Color::FOOD), 0);
	lv_obj_set_style_text_font(m_gameOverLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(m_gameOverLabel, LV_ALIGN_CENTER, 0, -160);
	lv_obj_add_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);

	// 重新开始按钮（初始隐藏）
	m_restartBtn = lv_button_create(screen);
	lv_obj_set_size(m_restartBtn, 200, 60);
	lv_obj_set_style_radius(m_restartBtn, 16, 0);
	lv_obj_set_style_bg_color(m_restartBtn, lv_color_hex(Color::BTN_1P), 0);
	lv_obj_set_style_bg_opa(m_restartBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_restartBtn, 12, 0);
	lv_obj_set_style_shadow_color(m_restartBtn, lv_color_hex(Color::BTN_1P), 0);
	lv_obj_set_style_shadow_opa(m_restartBtn, LV_OPA_40, 0);
	lv_obj_set_style_border_width(m_restartBtn, 0, 0);
	lv_obj_set_style_outline_width(m_restartBtn, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_restartBtn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(m_restartBtn, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_restartBtn, 3, LV_STATE_FOCUSED);
	lv_obj_align(m_restartBtn, LV_ALIGN_CENTER, 0, 30);
	auto lblRestart = lv_label_create(m_restartBtn);
	lv_label_set_text(lblRestart, "重新开始");
	lv_obj_set_style_text_color(lblRestart, lv_color_hex(0x000000), 0);
	lv_obj_center(lblRestart);
	lv_obj_add_event_cb(m_restartBtn, btnRestartCb, LV_EVENT_CLICKED, this);
	lv_obj_add_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);

	// 游戏结束得分标签（初始隐藏，大红色字体）
	m_gameOverScoreLabel = lv_label_create(screen);
	lv_label_set_text(m_gameOverScoreLabel, "");
	lv_obj_set_style_text_color(m_gameOverScoreLabel, lv_color_hex(Color::FOOD), 0);
	lv_obj_set_style_text_font(m_gameOverScoreLabel, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(m_gameOverScoreLabel, LV_ALIGN_CENTER, 0, -80);
	lv_obj_add_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);

	// 返回菜单按钮（初始隐藏）
	m_backBtn = lv_button_create(screen);
	lv_obj_set_size(m_backBtn, 200, 60);
	lv_obj_set_style_radius(m_backBtn, 16, 0);
	lv_obj_set_style_bg_color(m_backBtn, lv_color_hex(0xff555566), 0);
	lv_obj_set_style_bg_opa(m_backBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_shadow_width(m_backBtn, 12, 0);
	lv_obj_set_style_shadow_color(m_backBtn, lv_color_hex(0xff555566), 0);
	lv_obj_set_style_shadow_opa(m_backBtn, LV_OPA_40, 0);
	lv_obj_set_style_border_width(m_backBtn, 0, 0);
	lv_obj_set_style_outline_width(m_backBtn, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(m_backBtn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(m_backBtn, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(m_backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_align(m_backBtn, LV_ALIGN_CENTER, 0, 110);
	auto lblBack = lv_label_create(m_backBtn);
	lv_label_set_text(lblBack, "返回菜单");
	lv_obj_set_style_text_color(lblBack, lv_color_hex(Color::TEXT), 0);
	lv_obj_center(lblBack);
	lv_obj_add_event_cb(m_backBtn, btnBackCb, LV_EVENT_CLICKED, this);
	lv_obj_add_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);

	// 使用网格尺寸初始化游戏逻辑
	m_logic = SnakeGameLogic{ m_playerCount, m_gridW, m_gridH };
	m_logic.reset();
	m_logic.setState(SnakeGameLogic::State::Playing);
	m_foodCount = 1;

	lv_label_set_text(m_statusLabel, "D-pad 控制方向");

	ESP_LOGI(TAG, "Starting %dP game, grid: %dx%d", m_playerCount, m_gridW, m_gridH);

	// TODO: 远程导航 — 后续参考 tetris 分支的 WS 架构重新实现
	// wsServerRegisterGameCallback(gameKeyCb, this);

	// 启动游戏线程
	running = true;
	m_thread = Thread{ gameLoop, "snakeGame", this, Thread::Priority::Normal, 8192 };
}

void SnakeGame::deinit()
{
	running = false;
	// TODO: 远程导航 — 后续参考 tetris 分支的 WS 架构重新实现
	// wsServerUnregisterGameCallback();
	vTaskDelay(pdMS_TO_TICKS(200));
	m_thread = {};

	// LVGL 对象由屏幕销毁时自动回收，无需显式释放

	deletable = true;
}

// ============================================================
// 对象池创建（蛇身段 + 食物）
// ============================================================

void SnakeGame::createObjectPool(lv_obj_t* parent)
{
	// 计算网格尺寸（基于实际分辨率）
	int canvasW = lv_display_get_horizontal_resolution(display->getLvglDisplay());
	int canvasH = lv_display_get_vertical_resolution(display->getLvglDisplay());
	m_cellSize = 20;
	m_gridW = canvasW / m_cellSize;
	m_gridH = canvasH / m_cellSize;
	if (m_gridW < 10) m_gridW = 10;
	if (m_gridH < 10) m_gridH = 10;

	// ── 网格线（纯色 1px 细线，一次性创建） ──
	for (int gx = 0; gx <= m_gridW; gx++)
	{
		m_gridLinesV[gx] = lv_obj_create(parent);
		lv_obj_set_size(m_gridLinesV[gx], 1, m_gridH * m_cellSize);
		lv_obj_set_pos(m_gridLinesV[gx], gx * m_cellSize, 0);
		lv_obj_set_style_bg_color(m_gridLinesV[gx], lv_color_hex(Color::GRID_LINE), 0);
		lv_obj_set_style_bg_opa(m_gridLinesV[gx], LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(m_gridLinesV[gx], 0, 0);
		lv_obj_remove_flag(m_gridLinesV[gx], LV_OBJ_FLAG_CLICKABLE);
	}
	for (int gy = 0; gy <= m_gridH; gy++)
	{
		m_gridLinesH[gy] = lv_obj_create(parent);
		lv_obj_set_size(m_gridLinesH[gy], m_gridW * m_cellSize, 1);
		lv_obj_set_pos(m_gridLinesH[gy], 0, gy * m_cellSize);
		lv_obj_set_style_bg_color(m_gridLinesH[gy], lv_color_hex(Color::GRID_LINE), 0);
		lv_obj_set_style_bg_opa(m_gridLinesH[gy], LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(m_gridLinesH[gy], 0, 0);
		lv_obj_remove_flag(m_gridLinesH[gy], LV_OBJ_FLAG_CLICKABLE);
	}

	ESP_LOGI(TAG, "Grid: %d vertical + %d horizontal lines", m_gridW + 1, m_gridH + 1);

	// ── 蛇身段对象池 ──
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		uint32_t color = (p == 0) ? Color::SNAKE1 :
			(p == 1) ? Color::SNAKE2 :
			(p == 2) ? Color::SNAKE3 : Color::SNAKE4;
		uint32_t glowColor = (p == 0) ? Color::SNAKE1_GLOW :
			(p == 1) ? Color::SNAKE2_GLOW :
			(p == 2) ? Color::SNAKE3_GLOW : Color::SNAKE4_GLOW;

		for (int i = 0; i < MAX_SEGMENTS; i++)
		{
			m_segments[p][i] = lv_obj_create(parent);
			lv_obj_set_style_radius(m_segments[p][i], 4, 0);
			lv_obj_set_style_bg_color(m_segments[p][i], lv_color_hex(color), 0);
			lv_obj_set_style_bg_opa(m_segments[p][i], LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(m_segments[p][i], 0, 0);
			lv_obj_remove_flag(m_segments[p][i], LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(m_segments[p][i], LV_OBJ_FLAG_HIDDEN);

			if (i == 0)
			{
				// 蛇头：略大 + 发光 shadow
				lv_obj_set_size(m_segments[p][i], m_cellSize, m_cellSize);
				lv_obj_set_style_shadow_width(m_segments[p][i], 10, 0);
				lv_obj_set_style_shadow_color(m_segments[p][i], lv_color_hex(glowColor), 0);
				lv_obj_set_style_shadow_opa(m_segments[p][i], LV_OPA_40, 0);
			}
			else
			{
				lv_obj_set_size(m_segments[p][i], m_cellSize - 2, m_cellSize - 2);
			}
		}
	}

	// ── 蛇头呼吸动画（transform_scale，居中缩放） ──
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		auto* head = m_segments[p][0];
		// LVGL 9: LV_SCALE_NONE = 256 = 100%
		constexpr int SCALE_100 = 256;
		lv_obj_set_style_transform_pivot_x(head, m_cellSize / 2, 0);
		lv_obj_set_style_transform_pivot_y(head, m_cellSize / 2, 0);

		lv_anim_t a;
		lv_anim_init(&a);
		lv_anim_set_var(&a, head);
		lv_anim_set_values(&a, SCALE_100 * 0.8, SCALE_100 * 1.1);
		lv_anim_set_time(&a, 300);
		lv_anim_set_playback_time(&a, 300);
		lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
		lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
			lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), v, 0);
			});
		lv_anim_start(&a);
	}

	// ── 蛇头眼睛（直接放在 screen 上，用绝对坐标） ──
	for (int p = 0; p < MAX_PLAYERS; p++)
	{
		auto mkEye = [&](lv_obj_t*& eye) {
			eye = lv_obj_create(parent);
			lv_obj_set_size(eye, 6, 6);
			lv_obj_set_style_radius(eye, 3, 0);
			lv_obj_set_style_bg_color(eye, lv_color_hex(0xFFFFFFFF), 0);
			lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(eye, 1, 0);
			lv_obj_set_style_border_color(eye, lv_color_hex(0xFF333333), 0);
			lv_obj_remove_flag(eye, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(eye, LV_OBJ_FLAG_HIDDEN);
			};
		mkEye(m_headEyeL[p]);
		mkEye(m_headEyeR[p]);
	}

	// ── 食物对象池（圆形 + 发光 shadow） ──
	for (int i = 0; i < MAX_FOOD_ITEMS; i++)
	{
		m_foodItems[i] = lv_obj_create(parent);
		lv_obj_set_size(m_foodItems[i], m_cellSize, m_cellSize);
		lv_obj_set_style_radius(m_foodItems[i], m_cellSize / 2, 0);
		lv_obj_set_style_bg_color(m_foodItems[i], lv_color_hex(Color::FOOD), 0);
		lv_obj_set_style_bg_opa(m_foodItems[i], LV_OPA_COVER, 0);
		lv_obj_set_style_shadow_width(m_foodItems[i], 14, 0);
		lv_obj_set_style_shadow_color(m_foodItems[i], lv_color_hex(Color::FOOD_GLOW), 0);
		lv_obj_set_style_shadow_opa(m_foodItems[i], LV_OPA_40, 0);
		lv_obj_set_style_border_width(m_foodItems[i], 0, 0);
		lv_obj_remove_flag(m_foodItems[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_foodItems[i], LV_OBJ_FLAG_HIDDEN);
	}

	ESP_LOGI(TAG, "Object pool created: %dx%d grid (cell=%d), %d segs/player, %d foods",
		m_gridW, m_gridH, m_cellSize, MAX_SEGMENTS, MAX_FOOD_ITEMS);
}

// ============================================================
// D-pad 创建
// ============================================================

void SnakeGame::createDpad(lv_obj_t* parent)
{
	constexpr int S = 56;   // 按钮尺寸
	constexpr int G = 4;    // 间距
	constexpr int R = 14;   // 圆角半径
	constexpr int PAD = 12; // 容器内边�?
	constexpr int W = S * 3 + G * 2 + PAD * 2;
	constexpr int H = S * 3 + G * 2 + PAD * 2;

	// 辅助：创建圆形玻璃按�?
	auto mkBtn = [&](lv_obj_t* pad, lv_obj_t*& btn, int x, int y, const char* txt,
		lv_event_cb_t cb, uint32_t bgColor)
		{
			btn = lv_button_create(pad);
			lv_obj_set_size(btn, S, S);
			lv_obj_set_style_radius(btn, R, 0);
			lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
			lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
			lv_obj_set_style_border_width(btn, 1, 0);
			lv_obj_set_style_border_color(btn, lv_color_hex(0x44ffffff), 0);
			lv_obj_set_style_border_opa(btn, LV_OPA_50, 0);
			lv_obj_set_style_shadow_width(btn, 8, 0);
			lv_obj_set_style_shadow_color(btn, lv_color_hex(bgColor), 0);
			lv_obj_set_style_shadow_opa(btn, LV_OPA_30, 0);
			lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
			lv_obj_set_pos(btn, x + PAD, y + PAD);

			auto lbl = lv_label_create(btn);
			lv_label_set_text(lbl, txt);
			lv_obj_set_style_text_color(lbl, lv_color_hex(Color::DPAD_ARROW), 0);
			lv_obj_set_style_text_font(lbl, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
			lv_obj_center(lbl);
			lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
		};

	// ====== 玩家 1 D-pad（右下角，半透明叠加�?======
	m_pad1 = lv_obj_create(parent);
	lv_obj_set_size(m_pad1, W, H);
	lv_obj_set_style_bg_color(m_pad1, lv_color_hex(Color::DPAD_BG), 0);
	lv_obj_set_style_bg_opa(m_pad1, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_pad1, 0, 0);
	lv_obj_set_style_radius(m_pad1, 20, 0);
	lv_obj_set_style_pad_all(m_pad1, 0, 0);
	lv_obj_align(m_pad1, LV_ALIGN_BOTTOM_RIGHT, -16, -16);

	mkBtn(m_pad1, m_p1Up, S + G, 0, "▲", btnP1UpCb, Color::DPAD_BTN);
	mkBtn(m_pad1, m_p1Left, 0, S + G, "◀", btnP1LeftCb, Color::DPAD_BTN);
	mkBtn(m_pad1, m_p1Right, S * 2 + G, S + G, "▶", btnP1RightCb, Color::DPAD_BTN);
	mkBtn(m_pad1, m_p1Down, S + G, S * 2 + G, "▼", btnP1DownCb, Color::DPAD_BTN);

	// 暂停按钮（中心，更小�?
	m_p1Pause = lv_button_create(m_pad1);
	lv_obj_set_size(m_p1Pause, 36, 36);
	lv_obj_set_style_radius(m_p1Pause, 18, 0);
	lv_obj_set_style_bg_color(m_p1Pause, lv_color_hex(0x66444455), 0);
	lv_obj_set_style_border_width(m_p1Pause, 1, 0);
	lv_obj_set_style_border_color(m_p1Pause, lv_color_hex(0x44ffffff), 0);
	lv_obj_set_style_outline_width(m_p1Pause, 0, LV_STATE_FOCUSED);
	lv_obj_set_pos(m_p1Pause, S + G + PAD + 10, S + G + PAD + 10);
	auto lblP = lv_label_create(m_p1Pause);
	lv_label_set_text(lblP, "||");
	lv_obj_set_style_text_color(lblP, lv_color_hex(Color::SUBTLE), 0);
	lv_obj_center(lblP);
	lv_obj_add_event_cb(m_p1Pause, btnPauseCb, LV_EVENT_CLICKED, this);

	// ====== 玩家 2 D-pad（左下角，半透明叠加�?======
	m_pad2 = lv_obj_create(parent);
	lv_obj_set_size(m_pad2, W, H);
	lv_obj_set_style_bg_color(m_pad2, lv_color_hex(Color::DPAD_BG), 0);
	lv_obj_set_style_bg_opa(m_pad2, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_pad2, 0, 0);
	lv_obj_set_style_radius(m_pad2, 20, 0);
	lv_obj_set_style_pad_all(m_pad2, 0, 0);
	lv_obj_align(m_pad2, LV_ALIGN_BOTTOM_LEFT, 16, -16);
	lv_obj_add_flag(m_pad2, LV_OBJ_FLAG_HIDDEN);

	mkBtn(m_pad2, m_p2Up, S + G, 0, "▲", btnP2UpCb, Color::DPAD_BTN);
	mkBtn(m_pad2, m_p2Left, 0, S + G, "◀", btnP2LeftCb, Color::DPAD_BTN);
	mkBtn(m_pad2, m_p2Right, S * 2 + G, S + G, "▶", btnP2RightCb, Color::DPAD_BTN);
	mkBtn(m_pad2, m_p2Down, S + G, S * 2 + G, "▼", btnP2DownCb, Color::DPAD_BTN);

	// ====== 玩家 3 D-pad（右上角，橙色） ======
	m_pad3 = lv_obj_create(parent);
	lv_obj_set_size(m_pad3, W, H);
	lv_obj_set_style_bg_color(m_pad3, lv_color_hex(Color::DPAD_BG), 0);
	lv_obj_set_style_bg_opa(m_pad3, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_pad3, 0, 0);
	lv_obj_set_style_radius(m_pad3, 20, 0);
	lv_obj_set_style_pad_all(m_pad3, 0, 0);
	lv_obj_align(m_pad3, LV_ALIGN_TOP_RIGHT, -16, 16);
	lv_obj_add_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);

	mkBtn(m_pad3, m_p3Up, S + G, 0, "▲", btnP3UpCb, Color::DPAD_BTN);
	mkBtn(m_pad3, m_p3Left, 0, S + G, "◀", btnP3LeftCb, Color::DPAD_BTN);
	mkBtn(m_pad3, m_p3Right, S * 2 + G, S + G, "▶", btnP3RightCb, Color::DPAD_BTN);
	mkBtn(m_pad3, m_p3Down, S + G, S * 2 + G, "▼", btnP3DownCb, Color::DPAD_BTN);

	// ====== 玩家 4 D-pad（左上角，紫色） ======
	m_pad4 = lv_obj_create(parent);
	lv_obj_set_size(m_pad4, W, H);
	lv_obj_set_style_bg_color(m_pad4, lv_color_hex(Color::DPAD_BG), 0);
	lv_obj_set_style_bg_opa(m_pad4, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_pad4, 0, 0);
	lv_obj_set_style_radius(m_pad4, 20, 0);
	lv_obj_set_style_pad_all(m_pad4, 0, 0);
	lv_obj_align(m_pad4, LV_ALIGN_TOP_LEFT, 16, 16);
	lv_obj_add_flag(m_pad4, LV_OBJ_FLAG_HIDDEN);

	mkBtn(m_pad4, m_p4Up, S + G, 0, "▲", btnP4UpCb, Color::DPAD_BTN);
	mkBtn(m_pad4, m_p4Left, 0, S + G, "◀", btnP4LeftCb, Color::DPAD_BTN);
	mkBtn(m_pad4, m_p4Right, S * 2 + G, S + G, "▶", btnP4RightCb, Color::DPAD_BTN);
	mkBtn(m_pad4, m_p4Down, S + G, S * 2 + G, "▼", btnP4DownCb, Color::DPAD_BTN);
}

// ============================================================
// 更新场景（LVGL 原生对象，脏区域追踪）
// ============================================================

void SnakeGame::updateScene()
{
	const int CS = m_cellSize;

	// ============================================================
	// 1. 蛇身段 — 更新位置 + 显隐
	// ============================================================
	for (int p = 0; p < m_playerCount; p++)
	{
		const auto& snake = m_logic.getSnake(p);
		size_t bodySize = snake.body.size();

		for (size_t i = 0; i < MAX_SEGMENTS; i++)
		{
			if (i < bodySize)
			{
				lv_obj_set_pos(m_segments[p][i],
					snake.body[i].x * CS,
					snake.body[i].y * CS);
				lv_obj_clear_flag(m_segments[p][i], LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(m_segments[p][i], LV_OBJ_FLAG_HIDDEN);
			}
		}
		m_segmentCount[p] = bodySize;

		// 蛇头眼睛（绝对坐标 + 显隐）
		if (bodySize > 0 && snake.alive)
		{
			int hx = snake.body[0].x * CS;
			int hy = snake.body[0].y * CS;
			int exL = 10, eyL = 3, exR = 10, eyR = 11;  // 默认朝右
			switch (snake.direction)
			{
			case SnakeGameLogic::Direction::Left:
				exL = 4;  eyL = 3;  exR = 4;  eyR = 11; break;
			case SnakeGameLogic::Direction::Up:
				exL = 3;  eyL = 4;  exR = 11; eyR = 4;  break;
			case SnakeGameLogic::Direction::Down:
				exL = 3;  eyL = 10; exR = 11; eyR = 10; break;
			default: break;
			}
			lv_obj_set_pos(m_headEyeL[p], hx + exL, hy + eyL);
			lv_obj_set_pos(m_headEyeR[p], hx + exR, hy + eyR);
			lv_obj_clear_flag(m_headEyeL[p], LV_OBJ_FLAG_HIDDEN);
			lv_obj_clear_flag(m_headEyeR[p], LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_headEyeL[p], LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(m_headEyeR[p], LV_OBJ_FLAG_HIDDEN);
		}
	}

	// ============================================================
	// 2. 食物 — 更新位置 + 显隐
	// ============================================================
	for (int i = 0; i < MAX_FOOD_ITEMS; i++)
	{
		if (i < m_foodCount)
		{
			lv_obj_set_pos(m_foodItems[i],
				m_logic.getFood().x * CS,
				m_logic.getFood().y * CS);
			lv_obj_clear_flag(m_foodItems[i], LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_foodItems[i], LV_OBJ_FLAG_HIDDEN);
		}
	}

	// ============================================================
	// 3. 更新文本标签
	// ============================================================
	if (m_playerCount >= 4)
	{
		lv_label_set_text_fmt(m_scoreLabel, "P1: %d  P2: %d  P3: %d  P4: %d",
			m_logic.getSnake(0).score, m_logic.getSnake(1).score,
			m_logic.getSnake(2).score, m_logic.getSnake(3).score);
	}
	else if (m_playerCount >= 3)
	{
		lv_label_set_text_fmt(m_scoreLabel, "P1: %d  |  P2: %d  |  P3: %d",
			m_logic.getSnake(0).score, m_logic.getSnake(1).score,
			m_logic.getSnake(2).score);
	}
	else if (m_playerCount >= 2)
	{
		lv_label_set_text_fmt(m_scoreLabel, "P1: %d  |  P2: %d",
			m_logic.getSnake(0).score, m_logic.getSnake(1).score);
	}
	else
	{
		lv_label_set_text_fmt(m_scoreLabel, "分数: %d", m_logic.getSnake(0).score);
	}

	switch (m_logic.getState())
	{
	case SnakeGameLogic::State::Paused:
		lv_label_set_text(m_statusLabel, "暂停中");
		break;
	case SnakeGameLogic::State::GameOver:
	{
		// 得分移入中间大标签，顶部状态栏清空
		lv_label_set_text(m_statusLabel, "");
		int maxScore = -1, winner = -1;
		bool tie = false;
		for (int i = 0; i < m_logic.getPlayerCount(); i++)
		{
			int s = m_logic.getSnake(i).score;
			if (s > maxScore) { maxScore = s; winner = i; tie = false; }
			else if (s == maxScore) tie = true;
		}
		if (m_logic.getPlayerCount() >= 2 && !tie)
			lv_label_set_text_fmt(m_gameOverScoreLabel, "玩家 %d 胜利！", winner + 1);
		else if (tie)
			lv_label_set_text(m_gameOverScoreLabel, "平局！");
		else
			lv_label_set_text_fmt(m_gameOverScoreLabel, "得分: %d", m_logic.getSnake(0).score);
		break;
	}
	default:
		lv_label_set_text(m_statusLabel, "");
		break;
	}

	const bool isOver = (m_logic.getState() == SnakeGameLogic::State::GameOver);

	if (isOver)
	{
		lv_obj_clear_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);
		lv_obj_clear_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);
		applyFocus();
	}
	else
	{
		lv_obj_add_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);
	}
}

// ============================================================
// 焦点导航
// ============================================================

void SnakeGame::applyFocus()
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

void SnakeGame::activateFocus()
{
	switch (m_focusIdx)
	{
	case 0: lv_obj_send_event(m_restartBtn, LV_EVENT_CLICKED, nullptr); break;
	case 1: lv_obj_send_event(m_backBtn,   LV_EVENT_CLICKED, nullptr); break;
	}
}

void SnakeGame::onForeground()
{
	// 全置按下：下一帧 newPress = 0，跳过边沿检测
	m_prevButtons = 0xFFFF;
	for (auto& t : m_nextMoveTime) t = 0;
	m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
	m_focusIdx = 0;
	if (auto guard = display->lockGuard())
		applyFocus();
}

// ============================================================
// D-pad 按钮回调
// ============================================================

void SnakeGame::btnRestartCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	// 栈操作不能在 LVGL 事件中执行，延后处理
	Task::addTask([](void* p) -> TickType_t {
		auto* game = static_cast<SnakeGame*>(p);
		game->replaceWith(new SnakeGame(game->display, game->m_playerCount));
		return Task::infinityTime;
		}, "restartGame", self, 0, Task::Affinity::None);
}

void SnakeGame::btnBackCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	// 栈操作不能在 LVGL 事件中执行，延后处理
	Task::addTask([](void* p) -> TickType_t {
		static_cast<SnakeGame*>(p)->popApp();
		return Task::infinityTime;
		}, "backToRoom", self, 0, Task::Affinity::None);
}

void SnakeGame::setDirAndStart(SnakeGame* self, int player, SnakeGameLogic::Direction dir)
{
	// GameOver 时按方向键也重启（栈操作，延后处理）
	if (self->m_logic.getState() == SnakeGameLogic::State::GameOver)
	{
		Task::addTask([](void* p) -> TickType_t {
			auto* game = static_cast<SnakeGame*>(p);
			game->replaceWith(new SnakeGame(game->display, game->m_playerCount));
			return Task::infinityTime;
			}, "restartGame", self, 0, Task::Affinity::None);
		return;
	}

	self->m_logic.setDirection(player, dir);
}

void SnakeGame::btnP1UpCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Up);
}
void SnakeGame::btnP1DownCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Down);
}
void SnakeGame::btnP1LeftCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Left);
}
void SnakeGame::btnP1RightCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Right);
}

void SnakeGame::btnP2UpCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Up);
}
void SnakeGame::btnP2DownCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Down);
}
void SnakeGame::btnP2LeftCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Left);
}
void SnakeGame::btnP2RightCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Right);
}

void SnakeGame::btnP3UpCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Up);
}
void SnakeGame::btnP3DownCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Down);
}
void SnakeGame::btnP3LeftCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Left);
}
void SnakeGame::btnP3RightCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Right);
}

void SnakeGame::btnP4UpCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 3, SnakeGameLogic::Direction::Up);
}
void SnakeGame::btnP4DownCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 3, SnakeGameLogic::Direction::Down);
}
void SnakeGame::btnP4LeftCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 3, SnakeGameLogic::Direction::Left);
}
void SnakeGame::btnP4RightCb(lv_event_t* e)
{
	setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 3, SnakeGameLogic::Direction::Right);
}

void SnakeGame::btnPauseCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	auto state = self->m_logic.getState();
	if (state == SnakeGameLogic::State::Playing)
		self->m_logic.setState(SnakeGameLogic::State::Paused);
	else if (state == SnakeGameLogic::State::Paused)
		self->m_logic.setState(SnakeGameLogic::State::Playing);
	else if (state == SnakeGameLogic::State::GameOver)
	{
		// 栈操作，延后处理
		Task::addTask([](void* p) -> TickType_t {
			auto* game = static_cast<SnakeGame*>(p);
			game->replaceWith(new SnakeGame(game->display, game->m_playerCount));
			return Task::infinityTime;
			}, "restartGame", self, 0, Task::Affinity::None);
	}
}

// ============================================================
// 远程输入处理
// ============================================================

void SnakeGame::gameKeyCb(int player, uint8_t keyCode, bool pressed, void* ctx)
{
	auto& self = *static_cast<SnakeGame*>(ctx);
	if (!pressed) return;

	auto dir = SnakeGameLogic::Direction::None;
	switch (keyCode)
	{
	case 0: dir = SnakeGameLogic::Direction::Up;    break;
	case 1: dir = SnakeGameLogic::Direction::Down;  break;
	case 2: dir = SnakeGameLogic::Direction::Left;  break;
	case 3: dir = SnakeGameLogic::Direction::Right; break;
	case 4:
		if (self.m_logic.getState() == SnakeGameLogic::State::Waiting)
			self.m_logic.setState(SnakeGameLogic::State::Playing);
		break;
	case 5:
		if (self.m_logic.getState() == SnakeGameLogic::State::Playing)
			self.m_logic.setState(SnakeGameLogic::State::Paused);
		else if (self.m_logic.getState() == SnakeGameLogic::State::Paused)
			self.m_logic.setState(SnakeGameLogic::State::Playing);
		break;
	}

	if (dir != SnakeGameLogic::Direction::None)
	{
		int p = (player >= 0 && player < self.m_playerCount) ? player : 0;
		self.m_logic.setDirection(p, dir);
	}
}

// ============================================================
// 游戏主循环（独立线程�?
// ============================================================

void SnakeGame::gameLoop(void* param)
{
	auto& self = *static_cast<SnakeGame*>(param);
	ESP_LOGI(TAG, "游戏循环启动");

	int tickInterval = 80;   // ms，初始速度
	constexpr int MIN_INTERVAL = 40;

	while (self.running)
	{
		// 游戏逻辑（方向由 D-pad 按钮回调直接设置�?
		if (self.m_logic.getState() == SnakeGameLogic::State::Playing)
		{
			self.m_logic.tick();
			int speed = self.m_logic.getSnake(0).score / 5;
			tickInterval = 80 - speed * 8;
			if (tickInterval < MIN_INTERVAL) tickInterval = MIN_INTERVAL;
		}

		// 更新场景（LVGL 对象位置/显隐/文本）
		if (auto guard = self.display->lockGuard())
			self.updateScene();

		vTaskDelay(pdMS_TO_TICKS(tickInterval));
	}

	ESP_LOGI(TAG, "Game loop exit");
	self.deletable = true;

	while (true)
		vTaskDelay(5000); // 等待delete的时候删除
}

void SnakeGame::onGamepadInput(uint8_t playerId, const GamepadState& state)
{
	constexpr uint8_t deadZone = 50;
	constexpr uint8_t center = 128;

	bool lxLeft = (state.lx < center - deadZone);
	bool lxRight = (state.lx > center + deadZone);
	bool lyUp = (state.ly < center - deadZone);
	bool lyDown = (state.ly > center + deadZone);

	// ── 边沿检测：仅刚按下的按钮有效 ──
	uint16_t newPress = state.buttons & ~m_prevButtons;
	m_prevButtons = state.buttons;

	auto gameState = m_logic.getState();

	// ── BTN_B：GameOver 时返回菜单 ──
	if (newPress & static_cast<uint16_t>(GamepadButton::BTN_B))
	{
		if (gameState == SnakeGameLogic::State::GameOver)
		{
			Task::addTask([](void* p) -> TickType_t {
				static_cast<SnakeGame*>(p)->popApp();
				return Task::infinityTime;
			}, "backToRoom", this, 0, Task::Affinity::None);
			return;
		}
	}

	// ── 激活 (BTN_A / BTN_L3) ──
	if ((newPress & static_cast<uint16_t>(GamepadButton::BTN_A)) ||
		(newPress & static_cast<uint16_t>(GamepadButton::BTN_L3)))
	{
		if (m_nextActionTime < xTaskGetTickCount())
		{
			m_nextActionTime = xTaskGetTickCount() + ACTION_DELAY;
			if (gameState == SnakeGameLogic::State::GameOver)
			{
				activateFocus();
				return;
			}
			else if (gameState == SnakeGameLogic::State::Paused)
				m_logic.setState(SnakeGameLogic::State::Playing);
			else if (gameState == SnakeGameLogic::State::Playing)
				m_logic.setState(SnakeGameLogic::State::Paused);
		}
	}

	// ── 摇杆归位判断 ──
	if (!lxLeft && !lxRight && !lyUp && !lyDown)
	{
		m_nextMoveTime[playerId] = 0;
		return;
	}
	if (m_nextMoveTime[playerId] >= xTaskGetTickCount()) return;

	TickType_t delay = (m_nextMoveTime[playerId] == 0) ? MOVE_DELAY_FIRST : MOVE_DELAY;
	m_nextMoveTime[playerId] = xTaskGetTickCount() + delay;

	if (gameState == SnakeGameLogic::State::Playing)
	{
		if (lxLeft) setDirAndStart(this, playerId, SnakeGameLogic::Direction::Left);
		else if (lxRight) setDirAndStart(this, playerId, SnakeGameLogic::Direction::Right);
		else if (lyUp) setDirAndStart(this, playerId, SnakeGameLogic::Direction::Up);
		else if (lyDown) setDirAndStart(this, playerId, SnakeGameLogic::Direction::Down);
	}
	else if (gameState == SnakeGameLogic::State::GameOver)
	{
		// ── 上下导航焦点 ──
		if (lyUp && m_focusIdx > 0)   m_focusIdx--;
		if (lyDown && m_focusIdx < 1) m_focusIdx++;

		auto guard = display->lockGuard();
		applyFocus();
	}
}