#include "snakeGame.hpp"
#include "display/font.hpp"
// TODO: 远程导航 — 后续参考 tetris 分支的 WS 架构重新实现
// #include "wsServer/wsServer.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

#include <cstring>

// ============================================================
// 颜色常量
// ============================================================
namespace Color {
	constexpr uint32_t BG_TOP     = 0xff0a0a1e;  // 背景渐变顶部
	constexpr uint32_t BG_BOT     = 0xff1a1a35;  // 背景渐变底部
	constexpr uint32_t GRID_LINE  = 0xff1e1e35;  // 网格线（更暗�?

	constexpr uint32_t SNAKE1     = 0xff00e676;  // P1 蛇身
	constexpr uint32_t SNAKE1_H   = 0xff69f0ae;  // P1 蛇头（亮色）
	constexpr uint32_t SNAKE1_GLOW = 0x2200e676; // P1 发光

	constexpr uint32_t SNAKE2     = 0xff448aff;  // P2 蛇身
	constexpr uint32_t SNAKE2_H   = 0xff82b1ff;  // P2 蛇头（亮色）
	constexpr uint32_t SNAKE2_GLOW = 0x22448aff; // P2 发光

	constexpr uint32_t SNAKE3     = 0xffffa726;  // P3 蛇身（橙色）
	constexpr uint32_t SNAKE3_H   = 0xffffcc80;  // P3 蛇头（亮色）
	constexpr uint32_t SNAKE3_GLOW = 0x22ffa726; // P3 发光

	constexpr uint32_t FOOD       = 0xffff5252;  // 食物主体
	constexpr uint32_t FOOD_GLOW  = 0x44ff5252;  // 食物光晕
	constexpr uint32_t FOOD_INNER = 0xffff8a80;  // 食物高光

	constexpr uint32_t DPAD_BG    = 0x221a1a2e;  // D-pad 容器背景（极淡）
	constexpr uint32_t DPAD_BTN   = 0x55333355;  // D-pad 按钮（~33%）
	constexpr uint32_t DPAD_BTN_P = 0x66444477;  // D-pad 按钮按下
	constexpr uint32_t DPAD_ARROW = 0xccffffff;  // D-pad 箭头颜色

	constexpr uint32_t TEXT       = 0xffffffff;
	constexpr uint32_t SUBTLE     = 0xff888899;
	constexpr uint32_t MENU_BG    = 0xff0a0a1e;
	constexpr uint32_t BTN_1P     = 0xff00c853;
	constexpr uint32_t BTN_2P     = 0xff448aff;
	constexpr uint32_t OVERLAY_BG = 0x88000000;  // 半透明遮罩
}

// ============================================================
// 快速像素写入（直接操作缓冲，比 lv_canvas_set_px 快上千倍）
// ============================================================
static inline void drawPixel(uint32_t* buf, int w, int h, int x, int y, uint32_t color)
{
	if (x < 0 || x >= w || y < 0 || y >= h) return;
	buf[y * w + x] = color;
}

static inline void fillRect(uint32_t* buf, int w, int h, int x, int y, int rw, int rh, uint32_t color)
{
	if (x < 0) { rw += x; x = 0; }
	if (y < 0) { rh += y; y = 0; }
	if (x + rw > w) rw = w - x;
	if (y + rh > h) rh = h - y;
	if (rw <= 0 || rh <= 0) return;

	for (int row = 0; row < rh; row++)
	{
		int idx = (y + row) * w + x;
		for (int col = 0; col < rw; col++)
			buf[idx + col] = color;
	}
}

// ============================================================
// SnakeGame
// ============================================================

SnakeGame::SnakeGame(Display* display)
	: App(display)
	, m_logic{ 1 }  // 默认 1P，startGame 会重�?
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

	// 创建画布（铺满全屏）
	createCanvas(screen);
	lv_obj_align(m_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_add_flag(m_canvas, LV_OBJ_FLAG_HIDDEN);

	// 创建模式选择菜单（进入游戏前显示�?
	createMenu(screen);

	// 创建 D-pad（初始隐藏）
	createDpad(screen);
	lv_obj_add_flag(lv_obj_get_parent(m_p1Up), LV_OBJ_FLAG_HIDDEN);

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
	lv_obj_align(m_gameOverLabel, LV_ALIGN_CENTER, 0, -140);
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
	lv_obj_set_style_outline_width(m_restartBtn, 0, LV_STATE_FOCUSED);
	lv_obj_align(m_restartBtn, LV_ALIGN_CENTER, 0, -10);
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
	lv_obj_set_style_outline_width(m_backBtn, 0, LV_STATE_FOCUSED);
	lv_obj_align(m_backBtn, LV_ALIGN_CENTER, 0, 70);
	auto lblBack = lv_label_create(m_backBtn);
	lv_label_set_text(lblBack, "返回菜单");
	lv_obj_set_style_text_color(lblBack, lv_color_hex(Color::TEXT), 0);
	lv_obj_center(lblBack);
	lv_obj_add_event_cb(m_backBtn, btnBackCb, LV_EVENT_CLICKED, this);
	lv_obj_add_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);

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

	// 释放画布缓冲
	if (m_canvasBuf)
	{
		heap_caps_free(m_canvasBuf);
		m_canvasBuf = nullptr;
	}

	deletable = true;
}

// ============================================================
// 模式选择菜单
// ============================================================

void SnakeGame::createMenu(lv_obj_t* parent)
{
	m_menu = lv_obj_create(parent);
	lv_obj_set_size(m_menu, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(m_menu, lv_color_hex(Color::MENU_BG), 0);
	lv_obj_set_style_bg_opa(m_menu, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_menu, 0, 0);
	lv_obj_set_style_pad_all(m_menu, 0, 0);

	// 标题
	auto title = lv_label_create(m_menu);
	lv_label_set_text(title, "贪吃蛇");
	lv_obj_set_style_text_color(title, lv_color_hex(Color::TEXT), 0);
	lv_obj_set_style_text_font(title, FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

	// 副标�?
	auto sub = lv_label_create(m_menu);
	lv_label_set_text(sub, "选择游戏模式");
	lv_obj_set_style_text_color(sub, lv_color_hex(Color::SUBTLE), 0);
	lv_obj_set_style_text_font(sub, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 120);

	auto mkMenuBtn = [&](lv_obj_t*& btn, const char* text, uint32_t bgColor, lv_event_cb_t cb, int yOff)
	{
		btn = lv_button_create(m_menu);
		lv_obj_set_size(btn, 280, 76);
		lv_obj_set_style_radius(btn, 20, 0);
		lv_obj_set_style_bg_color(btn, lv_color_hex(bgColor), 0);
		lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
		lv_obj_set_style_shadow_width(btn, 16, 0);
		lv_obj_set_style_shadow_color(btn, lv_color_hex(bgColor), 0);
		lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);
		lv_obj_set_style_border_width(btn, 0, 0);
		lv_obj_set_style_outline_width(btn, 0, LV_STATE_FOCUSED);
		lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, yOff);

		auto lbl = lv_label_create(btn);
		lv_label_set_text(lbl, text);
		lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
		lv_obj_set_style_text_font(lbl, FontLoader::getDefault(FontLoader::FontSize::Default), 0);
		lv_obj_center(lbl);
		lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
	};

	mkMenuBtn(m_btn1P, "1 人游戏", Color::BTN_1P, btn1PCb, 200);
	mkMenuBtn(m_btn2P, "2 人游戏", Color::BTN_2P, btn2PCb, 300);
	mkMenuBtn(m_btn3P, "3 人游戏", Color::SNAKE3, btn3PCb, 400);

	// 底部提示
	auto hint = lv_label_create(m_menu);
	lv_label_set_text(hint, "选择方向键或点击 D-pad 控制");
	lv_obj_set_style_text_color(hint, lv_color_hex(0xff555566), 0);
	lv_obj_set_style_text_font(hint, FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 650);
}

void SnakeGame::startGame(int playerCount)
{
	m_playerCount = playerCount;

	// 使用动态网格尺寸重置游戏逻辑
	m_logic = SnakeGameLogic{ playerCount, m_gridW, m_gridH };
	m_logic.reset();
	m_logic.setState(SnakeGameLogic::State::Playing);

	// 隐藏菜单，显示游�?UI
	lv_obj_add_flag(m_menu, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_canvas, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_statusLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_clear_flag(m_pad1, LV_OBJ_FLAG_HIDDEN);

	if (playerCount >= 2)
		lv_obj_clear_flag(m_pad2, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(m_pad2, LV_OBJ_FLAG_HIDDEN);

	if (playerCount >= 3)
		lv_obj_clear_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);

	lv_label_set_text(m_statusLabel, "D-pad 控制方向");

	ESP_LOGI(TAG, "%dP game start, grid: %dx%d", playerCount, m_gridW, m_gridH);
}

// ============================================================
// Canvas 创建
// ============================================================

void SnakeGame::createCanvas(lv_obj_t* parent)
{
	// 直接使用显示器的实际分辨率（已处理旋转）
	m_canvasW = lv_display_get_horizontal_resolution(display->getLvglDisplay());
	m_canvasH = lv_display_get_vertical_resolution(display->getLvglDisplay());

	// 计算网格尺寸
	m_cellSize = 20;
	m_gridW = m_canvasW / m_cellSize;
	m_gridH = m_canvasH / m_cellSize;

	if (m_gridW < 10) m_gridW = 10;
	if (m_gridH < 10) m_gridH = 10;

	// LVGL 使用 ARGB8888，每像素 4 字节
	size_t bufSize = m_canvasW * m_canvasH * sizeof(uint32_t);
	m_canvasBuf = (lv_color_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
	if (!m_canvasBuf)
	{
		ESP_LOGE(TAG, "Failed to alloc canvas buf (%zu bytes)", bufSize);
		return;
	}

	m_canvas = lv_canvas_create(parent);
	lv_canvas_set_buffer(m_canvas, m_canvasBuf, m_canvasW, m_canvasH, LV_COLOR_FORMAT_ARGB8888);
	lv_obj_set_style_radius(m_canvas, 0, 0);

	ESP_LOGI(TAG, "Canvas created: %dx%d, grid: %dx%d (cell=%d), buf=%zu bytes",
		m_canvasW, m_canvasH, m_gridW, m_gridH, m_cellSize, bufSize);
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

	mkBtn(m_pad1, m_p1Up,    S + G, 0,      "▲", btnP1UpCb,    Color::DPAD_BTN);
	mkBtn(m_pad1, m_p1Left,  0,      S + G, "◀", btnP1LeftCb,  Color::DPAD_BTN);
	mkBtn(m_pad1, m_p1Right, S*2+G, S + G, "▶", btnP1RightCb, Color::DPAD_BTN);
	mkBtn(m_pad1, m_p1Down,  S + G, S*2+G, "▼", btnP1DownCb,  Color::DPAD_BTN);

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

	mkBtn(m_pad2, m_p2Up,    S + G, 0,      "▲", btnP2UpCb,    Color::DPAD_BTN);
	mkBtn(m_pad2, m_p2Left,  0,      S + G, "◀", btnP2LeftCb,  Color::DPAD_BTN);
	mkBtn(m_pad2, m_p2Right, S*2+G, S + G, "▶", btnP2RightCb, Color::DPAD_BTN);
	mkBtn(m_pad2, m_p2Down,  S + G, S*2+G, "▼", btnP2DownCb,  Color::DPAD_BTN);

	// ====== 玩家 3 D-pad（底部中间，橙色） ======
	m_pad3 = lv_obj_create(parent);
	lv_obj_set_size(m_pad3, W, H);
	lv_obj_set_style_bg_color(m_pad3, lv_color_hex(Color::DPAD_BG), 0);
	lv_obj_set_style_bg_opa(m_pad3, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_pad3, 0, 0);
	lv_obj_set_style_radius(m_pad3, 20, 0);
	lv_obj_set_style_pad_all(m_pad3, 0, 0);
	lv_obj_align(m_pad3, LV_ALIGN_BOTTOM_MID, 0, -16);
	lv_obj_add_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);

	mkBtn(m_pad3, m_p3Up,    S + G, 0,      "▲", btnP3UpCb,    Color::DPAD_BTN);
	mkBtn(m_pad3, m_p3Left,  0,      S + G, "◀", btnP3LeftCb,  Color::DPAD_BTN);
	mkBtn(m_pad3, m_p3Right, S*2+G, S + G, "▶", btnP3RightCb, Color::DPAD_BTN);
	mkBtn(m_pad3, m_p3Down,  S + G, S*2+G, "▼", btnP3DownCb,  Color::DPAD_BTN);
}

// ============================================================
// 渲染 �?直接操作缓冲（零 lv_canvas_set_px 调用�?
// ============================================================

void SnakeGame::renderFrame()
{
	if (!m_canvas || !m_canvasBuf) return;

	auto* buf = (uint32_t*)m_canvasBuf;
	const int W = m_canvasW;
	const int H = m_canvasH;
	const int CS = m_cellSize;
	const int GW = m_gridW;
	const int GH = m_gridH;

	// ============================================================
	// 1. 填充纯色背景（简单循环，编译器会优化为快速 memset）
	// ============================================================
	uint32_t bgCol = 0xff0a0a1e;
	for (int i = 0; i < W * H; i++)
		buf[i] = bgCol;

	// ============================================================
	// 2. 网格线（暗色虚线）
	// ============================================================
	for (int gx = 0; gx <= GW; gx++)
	{
		int px = gx * CS;
		if (px >= W) break;
		for (int y = 0; y < H; y += 4)
			drawPixel(buf, W, H, px, y, Color::GRID_LINE);
	}
	for (int gy = 0; gy <= GH; gy++)
	{
		int py = gy * CS;
		if (py >= H) break;
		for (int x = 0; x < W; x += 4)
			drawPixel(buf, W, H, x, py, Color::GRID_LINE);
	}

	// ============================================================
	// 3. 食物（发光圆�?+ 内部高光�?
	// ============================================================
	{
		int fx = m_logic.getFood().x * CS;
		int fy = m_logic.getFood().y * CS;
		int half = CS / 2;
		int cx = fx + half;
		int cy = fy + half;

		// 外层光晕
		for (int dy = -half - 2; dy <= half + 2; dy++)
		{
			for (int dx = -half - 2; dx <= half + 2; dx++)
			{
				int dist = dx * dx + dy * dy;
				if (dist <= (half + 2) * (half + 2) && dist > (half - 2) * (half - 2))
					drawPixel(buf, W, H, cx + dx, cy + dy, Color::FOOD_GLOW);
			}
		}

		// 主体�?
		for (int dy = -half + 1; dy <= half - 1; dy++)
		{
			for (int dx = -half + 1; dx <= half - 1; dx++)
			{
				if (dx * dx + dy * dy <= (half - 2) * (half - 2))
					drawPixel(buf, W, H, cx + dx, cy + dy, Color::FOOD);
			}
		}

		// 高光（左上小圆）
		for (int dy = -3; dy <= 1; dy++)
		{
			for (int dx = -3; dx <= 1; dx++)
			{
				if (dx * dx + dy * dy <= 6)
					drawPixel(buf, W, H, cx - 2 + dx, cy - 2 + dy, Color::FOOD_INNER);
			}
		}
	}

	// ============================================================
	// 4. 蛇（圆角方块 + 发光蛇头�?
	// ============================================================
	for (int p = 0; p < m_logic.getPlayerCount(); p++)
	{
		const auto& snake = m_logic.getSnake(p);
		if (!snake.alive && snake.body.empty()) continue;

		uint32_t bodyCol  = Color::SNAKE1;
		uint32_t headCol  = Color::SNAKE1_H;
		uint32_t glowCol  = Color::SNAKE1_GLOW;
		if (p == 1) { bodyCol = Color::SNAKE2; headCol = Color::SNAKE2_H; glowCol = Color::SNAKE2_GLOW; }
		if (p == 2) { bodyCol = Color::SNAKE3; headCol = Color::SNAKE3_H; glowCol = Color::SNAKE3_GLOW; }

		for (size_t i = 0; i < snake.body.size(); i++)
		{
			int px = snake.body[i].x * CS;
			int py = snake.body[i].y * CS;
			int margin = (i == 0) ? 2 : 3;  // 蛇头稍大
			uint32_t col = (i == 0) ? headCol : bodyCol;

			// 蛇头光晕
			if (i == 0)
			{
				fillRect(buf, W, H, px - 1, py - 1, CS, CS, glowCol);
			}

			// 圆角矩形主体（用 fillRect 近似�?
			fillRect(buf, W, H, px + margin, py + 1, CS - margin * 2, CS - 2, col);
			fillRect(buf, W, H, px + 1, py + margin, CS - 2, CS - margin * 2, col);

			// 四个角用像素填充（近似圆�?2px�?
			drawPixel(buf, W, H, px + 1, py + 1, col);
			drawPixel(buf, W, H, px + CS - 2, py + 1, col);
			drawPixel(buf, W, H, px + 1, py + CS - 2, col);
			drawPixel(buf, W, H, px + CS - 2, py + CS - 2, col);

			// 蛇头�?眼睛"（两个小白点�?
			if (i == 0 && CS >= 16)
			{
				int eyeOffX = 4, eyeOffY = 4, eyeR = 2;
				uint32_t eyeCol = 0xFFFFFFFF;
				for (int edy = -eyeR; edy <= eyeR; edy++)
					for (int edx = -eyeR; edx <= eyeR; edx++)
						if (edx * edx + edy * edy <= eyeR * eyeR)
						{
							drawPixel(buf, W, H, px + CS / 2 - eyeOffX + edx, py + eyeOffY + edy, eyeCol);
							drawPixel(buf, W, H, px + CS / 2 + eyeOffX + edx, py + eyeOffY + edy, eyeCol);
						}
			}
		}
	}

	// ============================================================
	// 5. 通知 LVGL 画布已更�?
	// ============================================================
	lv_obj_invalidate(m_canvas);

	// ============================================================
	// 6. 更新文本标签
	// ============================================================
	if (m_logic.getPlayerCount() >= 2)
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
	case SnakeGameLogic::State::Waiting:
		lv_label_set_text(m_statusLabel, "点击方向键开始");
		break;
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
// D-pad 按钮回调
// ============================================================

void SnakeGame::btn1PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	self->startGame(1);
}

void SnakeGame::btn2PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	self->startGame(2);
}

void SnakeGame::btn3PCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	self->startGame(3);
}

void SnakeGame::btnRestartCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	self->startGame(self->m_playerCount);
}

void SnakeGame::btnBackCb(lv_event_t* e)
{
	auto self = static_cast<SnakeGame*>(lv_event_get_user_data(e));
	self->goBackToMenu();
}

void SnakeGame::goBackToMenu()
{
	// 隐藏游戏 UI，显示菜单
	lv_obj_add_flag(m_canvas, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_statusLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_pad1, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_pad2, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_pad3, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_gameOverLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_gameOverScoreLabel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_restartBtn, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_backBtn, LV_OBJ_FLAG_HIDDEN);

	lv_obj_clear_flag(m_menu, LV_OBJ_FLAG_HIDDEN);

	// 重置游戏状态
	m_logic.setState(SnakeGameLogic::State::Waiting);
}

void SnakeGame::setDirAndStart(SnakeGame* self, int player, SnakeGameLogic::Direction dir)
{
	// GameOver 时按方向键也重启
	if (self->m_logic.getState() == SnakeGameLogic::State::GameOver)
	{
		self->startGame(self->m_playerCount);
		return;
	}

	self->m_logic.setDirection(player, dir);
	if (self->m_logic.getState() == SnakeGameLogic::State::Waiting)
		self->m_logic.setState(SnakeGameLogic::State::Playing);
}

void SnakeGame::btnP1UpCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Up); }
void SnakeGame::btnP1DownCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Down); }
void SnakeGame::btnP1LeftCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Left); }
void SnakeGame::btnP1RightCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 0, SnakeGameLogic::Direction::Right); }

void SnakeGame::btnP2UpCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Up); }
void SnakeGame::btnP2DownCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Down); }
void SnakeGame::btnP2LeftCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Left); }
void SnakeGame::btnP2RightCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 1, SnakeGameLogic::Direction::Right); }

void SnakeGame::btnP3UpCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Up); }
void SnakeGame::btnP3DownCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Down); }
void SnakeGame::btnP3LeftCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Left); }
void SnakeGame::btnP3RightCb(lv_event_t* e)
	{ setDirAndStart(static_cast<SnakeGame*>(lv_event_get_user_data(e)), 2, SnakeGameLogic::Direction::Right); }

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
		self->startGame(self->m_playerCount);
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
		if (self.m_logic.getState() == SnakeGameLogic::State::Waiting)
			self.m_logic.setState(SnakeGameLogic::State::Playing);
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

		// 渲染
		if (auto guard = self.display->lockGuard())
			self.renderFrame();

		vTaskDelay(pdMS_TO_TICKS(tickInterval));
	}

	ESP_LOGI(TAG, "Game loop exit");
	self.deletable = true;
}
