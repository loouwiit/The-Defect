#include "fruitNinjaRenderer.hpp"
#include "display/font.hpp"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <cmath>
#include <cstring>

static constexpr char TAG[] = "FruitNinjaRenderer";

constexpr FruitNinjaRenderer::FruitColors FruitNinjaRenderer::COLORS[];

// ============================================================
// 创建对象池
// ============================================================

void FruitNinjaRenderer::create(lv_obj_t* parent)
{
	// ── 动态层 canvas（全屏 PSRAM RGB565） ──
	const int W = FruitNinjaLogic::SCREEN_W;
	const int H = FruitNinjaLogic::SCREEN_H;
	const size_t bufSize = (size_t)W * H * 2;
	m_canvasBuf = static_cast<uint8_t*>(
		heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	assert(m_canvasBuf);

	m_canvas = lv_canvas_create(parent);
	lv_obj_remove_style_all(m_canvas);
	lv_obj_set_pos(m_canvas, 0, 0);
	lv_canvas_set_buffer(m_canvas, m_canvasBuf, W, H, LV_COLOR_FORMAT_RGB565);
	m_canvasDrawBuf = lv_canvas_get_draw_buf(m_canvas);
	m_canvasStride = m_canvasDrawBuf->header.stride;

	// 初始填充背景
	fillRect(0, 0, W, H, lv_color_to_u16(lv_color_hex(0xff0a0a1e)));

	// ── 水果提示标签（LVGL 对象，画在 canvas 之上） ──
	for (int i = 0; i < MAX_FRUITS; i++)
	{
		m_fruitLabels[i] = lv_label_create(parent);
		lv_label_set_text(m_fruitLabels[i], "");
		lv_obj_set_style_text_font(m_fruitLabels[i],
			FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(m_fruitLabels[i], lv_color_hex(0xffffffff), 0);
		lv_obj_remove_flag(m_fruitLabels[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_fruitLabels[i], LV_OBJ_FLAG_HIDDEN);
	}

	// ── HUD 标签 ──
	m_scoreLabel = lv_label_create(parent);
	lv_label_set_text(m_scoreLabel, "");
	lv_obj_set_style_text_color(m_scoreLabel, lv_color_hex(0xffffffff), 0);
	lv_obj_set_style_text_font(m_scoreLabel,
		FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_remove_flag(m_scoreLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);

	m_livesLabel = lv_label_create(parent);
	lv_label_set_text(m_livesLabel, "");
	lv_obj_set_style_text_color(m_livesLabel, lv_color_hex(0xffff5252), 0);
	lv_obj_set_style_text_font(m_livesLabel,
		FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_remove_flag(m_livesLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_livesLabel, LV_OBJ_FLAG_HIDDEN);

	m_timerLabel = lv_label_create(parent);
	lv_label_set_text(m_timerLabel, "");
	lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(0xffffa726), 0);
	lv_obj_set_style_text_font(m_timerLabel,
		FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_remove_flag(m_timerLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_timerLabel, LV_OBJ_FLAG_HIDDEN);

	m_hintLabel = lv_label_create(parent);
	lv_label_set_text(m_hintLabel, "");
	lv_obj_set_style_text_color(m_hintLabel, lv_color_hex(0xff888899), 0);
	lv_obj_set_style_text_font(m_hintLabel,
		FontLoader::getDefault(FontLoader::FontSize::Small), 0);
	lv_obj_align(m_hintLabel, LV_ALIGN_TOP_MID, 0, 8);
	lv_obj_remove_flag(m_hintLabel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_hintLabel, LV_OBJ_FLAG_HIDDEN);

	// ── GameOver 对象（初始隐藏） ──
	m_overlay = lv_obj_create(parent);
	lv_obj_set_size(m_overlay, lv_pct(100), lv_pct(100));
	lv_obj_set_style_bg_color(m_overlay, lv_color_hex(0x88000000), 0);
	lv_obj_set_style_bg_opa(m_overlay, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_overlay, 0, 0);
	lv_obj_remove_flag(m_overlay, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_overlay, LV_OBJ_FLAG_HIDDEN);

	m_gameOverTitle = lv_label_create(parent);
	lv_label_set_text(m_gameOverTitle, "游戏结束");
	lv_obj_set_style_text_color(m_gameOverTitle, lv_color_hex(0xffff5252), 0);
	lv_obj_set_style_text_font(m_gameOverTitle,
		FontLoader::getDefault(FontLoader::FontSize::Large), 0);
	lv_obj_remove_flag(m_gameOverTitle, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_gameOverTitle, LV_OBJ_FLAG_HIDDEN);

	m_gameOverScore = lv_label_create(parent);
	lv_label_set_text(m_gameOverScore, "");
	lv_obj_set_style_text_color(m_gameOverScore, lv_color_hex(0xffffffff), 0);
	lv_obj_set_style_text_font(m_gameOverScore,
		FontLoader::getDefault(FontLoader::FontSize::Default), 0);
	lv_obj_remove_flag(m_gameOverScore, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_gameOverScore, LV_OBJ_FLAG_HIDDEN);

	// ── 退出按钮（游戏中可见，右上角） ──
	m_exitBtn = lv_button_create(parent);
	lv_obj_set_size(m_exitBtn, 100, 44);
	lv_obj_set_style_radius(m_exitBtn, 12, 0);
	lv_obj_set_style_bg_color(m_exitBtn, lv_color_hex(0xaa333355), 0);
	lv_obj_set_style_bg_opa(m_exitBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_exitBtn, 0, 0);
	lv_obj_align(m_exitBtn, LV_ALIGN_TOP_RIGHT, -16, 16);
	lv_obj_add_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);
	auto exitLbl = lv_label_create(m_exitBtn);
	lv_label_set_text(exitLbl, "退出");
	lv_obj_set_style_text_color(exitLbl, lv_color_hex(0xffffffff), 0);
	lv_obj_center(exitLbl);

	ESP_LOGI(TAG, "canvas 创建完成: %dx%d, PSRAM %dKB", W, H, (int)(bufSize / 1024));
}

// ============================================================
// 每帧渲染：画动态层进 canvas + 更新 HUD/提示标签
// ============================================================

void FruitNinjaRenderer::render(const FruitNinjaLogic& logic)
{
	auto state = logic.getState();
	bool isPlaying = (state == FruitNinjaLogic::State::Playing);

	const int W = FruitNinjaLogic::SCREEN_W;
	const int H = FruitNinjaLogic::SCREEN_H;
	uint16_t bg = lv_color_to_u16(lv_color_hex(0xff0a0a1e));

	// ── 1. 计算脏矩形列表（水果/粒子各自 旧+新包围盒；刀光合并为 1 个） ──
	// 用多个小矩形而非单一合并框：避免水果铺满屏幕时合并框迅速退化为整屏。
	// 列表上限 MAX_DIRTY；溢出则塌缩为单一合并框（兜底，不遗漏）。
	constexpr int MAX_DIRTY = 24;
	int drX0[MAX_DIRTY], drY0[MAX_DIRTY], drX1[MAX_DIRTY], drY1[MAX_DIRTY];
	int dirtyCount = 0;
	bool dirtyOverflow = false;

	auto addDirty = [&](int x0, int y0, int x1, int y1) {
		if (dirtyOverflow) return;
		if (x1 < 0 || y1 < 0 || x0 >= W || y0 >= H) return;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= W) x1 = W - 1;
		if (y1 >= H) y1 = H - 1;
		// 与已有矩形重叠则合并（限制失效 pass 数）
		for (int i = 0; i < dirtyCount; i++)
		{
			if (x0 <= drX1[i] && x1 >= drX0[i] && y0 <= drY1[i] && y1 >= drY0[i])
			{
				if (x0 < drX0[i]) drX0[i] = x0;
				if (y0 < drY0[i]) drY0[i] = y0;
				if (x1 > drX1[i]) drX1[i] = x1;
				if (y1 > drY1[i]) drY1[i] = y1;
				return;
			}
		}
		if (dirtyCount < MAX_DIRTY)
		{
			drX0[dirtyCount] = x0; drY0[dirtyCount] = y0;
			drX1[dirtyCount] = x1; drY1[dirtyCount] = y1;
			dirtyCount++;
		}
		else
		{
			// 列表溢出 → 塌缩为单一大矩形
			dirtyOverflow = true;
			int ux0 = x0, uy0 = y0, ux1 = x1, uy1 = y1;
			for (int i = 0; i < dirtyCount; i++)
			{
				if (drX0[i] < ux0) ux0 = drX0[i];
				if (drY0[i] < uy0) uy0 = drY0[i];
				if (drX1[i] > ux1) ux1 = drX1[i];
				if (drY1[i] > uy1) uy1 = drY1[i];
			}
			dirtyCount = 1;
			drX0[0] = ux0; drY0[0] = uy0; drX1[0] = ux1; drY1[0] = uy1;
		}
	};

	int fruitCount = logic.getFruitCount();
	const auto* fruits = logic.getFruits();
	for (int i = 0; i < fruitCount; i++)
	{
		const auto& f = fruits[i];
		if (f.active)
		{
			int r = (int)FruitNinjaLogic::getFruitRadius(f.type);
			int cx = (int)f.x, cy = (int)f.y;
			addDirty(cx - r, cy - r, cx + r, cy + r);
			if (m_prevFruitActive[i])
				addDirty(m_prevFruitX[i] - m_prevFruitR[i], m_prevFruitY[i] - m_prevFruitR[i],
					m_prevFruitX[i] + m_prevFruitR[i], m_prevFruitY[i] + m_prevFruitR[i]);
			m_prevFruitX[i] = cx; m_prevFruitY[i] = cy; m_prevFruitR[i] = r;
			m_prevFruitActive[i] = true;
		}
		else if (m_prevFruitActive[i])
		{
			addDirty(m_prevFruitX[i] - m_prevFruitR[i], m_prevFruitY[i] - m_prevFruitR[i],
				m_prevFruitX[i] + m_prevFruitR[i], m_prevFruitY[i] + m_prevFruitR[i]);
			m_prevFruitActive[i] = false;
		}
	}

	int particleCount = logic.getParticleCount();
	const auto* particles = logic.getParticles();
	for (int i = 0; i < particleCount; i++)
	{
		const auto& p = particles[i];
		if (p.active)
		{
			int r = p.size / 2;
			int cx = (int)p.x, cy = (int)p.y;
			addDirty(cx - r, cy - r, cx + r, cy + r);
			if (m_prevParticleActive[i])
				addDirty(m_prevParticleX[i] - r, m_prevParticleY[i] - r,
					m_prevParticleX[i] + r, m_prevParticleY[i] + r);
			m_prevParticleX[i] = cx; m_prevParticleY[i] = cy;
			m_prevParticleActive[i] = true;
		}
		else if (m_prevParticleActive[i])
		{
			addDirty(m_prevParticleX[i] - 16, m_prevParticleY[i] - 16,
				m_prevParticleX[i] + 16, m_prevParticleY[i] + 16);
			m_prevParticleActive[i] = false;
		}
	}

	// 刀光：整条拖尾合并为 1 个包围盒（活跃段每帧淡出 + 刚失效段清残留）
	bool hasTrail = false;
	int tx0 = 0, ty0 = 0, tx1 = 0, ty1 = 0;
	for (int i = 0; i < kMaxSegments; i++)
	{
		const auto& s = m_segments[i];
		if (s.active || m_prevSegActive[i])
		{
			int x0 = (int)s.x1, y0 = (int)s.y1, x1 = (int)s.x2, y1 = (int)s.y2;
			if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
			if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
			x0 -= 8; y0 -= 8; x1 += 8; y1 += 8;
			if (!hasTrail) { tx0 = x0; ty0 = y0; tx1 = x1; ty1 = y1; hasTrail = true; }
			else {
				if (x0 < tx0) tx0 = x0;
				if (y0 < ty0) ty0 = y0;
				if (x1 > tx1) tx1 = x1;
				if (y1 > ty1) ty1 = y1;
			}
		}
		m_prevSegActive[i] = s.active;
	}
	if (hasTrail)
		addDirty(tx0, ty0, tx1, ty1);

	// ── 2. 清所有脏矩形（替代整屏清空） ──
	for (int i = 0; i < dirtyCount; i++)
		fillRect(drX0[i], drY0[i], drX1[i] - drX0[i] + 1, drY1[i] - drY0[i] + 1, bg);

	// ── 3. 画全部对象（整形状，越界自裁剪；脏区外内容本就正确） ──
	// 2P 分隔线（静态，代价极小）
	if (logic.getPlayerCount() >= 2)
	{
		uint16_t divColor = blend565(bg, 0xFFFF, 0x44);
		fillRect(W / 2 - 1, 0, 2, H, divColor);
	}

	// 水果（纯色圆；被切=半透明缩小）
	for (int i = 0; i < fruitCount; i++)
	{
		const auto& f = fruits[i];
		if (!f.active) continue;
		int typeIdx = static_cast<int>(f.type);
		if (typeIdx < 0 || typeIdx >= 5) typeIdx = 0;
		float r = FruitNinjaLogic::getFruitRadius(f.type);
		uint16_t color = lv_color_to_u16(lv_color_hex(COLORS[typeIdx].fill));
		int drawR = static_cast<int>(f.sliced ? r * 0.5f : r);
		if (drawR < 1) drawR = 1;
		fillCircle(static_cast<int>(f.x), static_cast<int>(f.y), drawR, color, f.opacity);
	}

	// 粒子
	for (int i = 0; i < particleCount; i++)
	{
		const auto& p = particles[i];
		if (!p.active) continue;
		int sz = p.size;
		if (sz < 1) sz = 1;
		uint16_t color = lv_color_to_u16(lv_color_make(p.r, p.g, p.b));
		fillCircle(static_cast<int>(p.x), static_cast<int>(p.y), sz / 2, color, p.opacity);
	}

	// 刀光（光晕粗线 + 刃芯实线，逐段淡出）
	for (int i = 0; i < kMaxSegments; i++)
	{
		const auto& s = m_segments[i];
		if (!s.active || s.opacity == 0) continue;
		int x1 = static_cast<int>(s.x1), y1 = static_cast<int>(s.y1);
		int x2 = static_cast<int>(s.x2), y2 = static_cast<int>(s.y2);
		drawLine(x1, y1, x2, y2, 14, 0xFFFF, static_cast<uint8_t>(s.opacity * 2 / 3)); // 光晕
		drawLine(x1, y1, x2, y2, 4, 0xFFFF, s.opacity);                                 // 刃芯
	}

	// ── 4. 失效脏矩形列表（屏幕坐标；canvas 位于 (0,0) 局部=屏幕） ──
	for (int i = 0; i < dirtyCount; i++)
	{
		lv_area_t a;
		lv_area_set(&a, drX0[i], drY0[i], drX1[i], drY1[i]);
		lv_obj_invalidate_area(m_canvas, &a);
	}

	// ── 5. HUD + 水果提示标签（LVGL 对象，画在 canvas 之上） ──
	updateHUD(lv_obj_get_parent(m_scoreLabel), logic);
	updateFruitLabels(logic, fruitCount, fruits, isPlaying);

	// ── 退出按钮（游戏中显示，GameOver 时隐藏） ──
	if (isPlaying)
		lv_obj_clear_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// HUD 更新
// ============================================================

void FruitNinjaRenderer::updateHUD(lv_obj_t* parent, const FruitNinjaLogic& logic)
{
	(void)parent;
	auto state = logic.getState();
	bool isPlaying = (state == FruitNinjaLogic::State::Playing);

	if (!isPlaying)
	{
		lv_obj_add_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_livesLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_timerLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_hintLabel, LV_OBJ_FLAG_HIDDEN);
		return;
	}

	// 分数：仅值变化时重设文字
	int pCount = logic.getPlayerCount();
	int s0 = logic.getScore(0), s1 = logic.getScore(1);
	if (pCount != m_lastPCount || s0 != m_lastScore0 || s1 != m_lastScore1)
	{
		m_lastPCount = pCount;
		m_lastScore0 = s0;
		m_lastScore1 = s1;
		if (pCount >= 2)
			lv_label_set_text_fmt(m_scoreLabel, "P1:%d  P2:%d", s0, s1);
		else
			lv_label_set_text_fmt(m_scoreLabel, "得分: %d", s0);
	}
	lv_obj_align(m_scoreLabel, LV_ALIGN_TOP_LEFT, 12, 8);
	lv_obj_clear_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);

	// 生命 / 计时：仅值变化时重设文字
	if (logic.getMode() == FruitNinjaLogic::GameMode::Classic)
	{
		int l0 = logic.getLives(0), l1 = logic.getLives(1);
		if (l0 != m_lastLives0 || l1 != m_lastLives1)
		{
			m_lastLives0 = l0;
			m_lastLives1 = l1;
			if (pCount >= 2)
				lv_label_set_text_fmt(m_livesLabel, "生命 P1:%d  P2:%d", l0, l1);
			else
				lv_label_set_text_fmt(m_livesLabel, "生命 x%d", l0);
		}
		lv_obj_align_to(m_livesLabel, m_scoreLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
		lv_obj_clear_flag(m_livesLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_timerLabel, LV_OBJ_FLAG_HIDDEN);
	}
	else // Arcade
	{
		int sec = static_cast<int>(logic.getTimeRemaining());
		if (sec != m_lastTimerSec)
		{
			m_lastTimerSec = sec;
			lv_label_set_text_fmt(m_timerLabel, "%02d", sec);
			if (sec <= 10)
				lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(0xffff5252), 0);
			else
				lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(0xffffa726), 0);
		}
		lv_obj_align(m_timerLabel, LV_ALIGN_TOP_MID, 0, 8);
		lv_obj_clear_flag(m_timerLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_livesLabel, LV_OBJ_FLAG_HIDDEN);
	}

	// 底部操作提示：常量，仅设置一次
	if (!m_hintSet)
	{
		m_hintSet = true;
		lv_label_set_text(m_hintLabel,
			"←↑→↓ + A/B/X/Y |  ↻ 触屏划动");
	}
	lv_obj_align(m_hintLabel, LV_ALIGN_BOTTOM_MID, 0, -4);
	lv_obj_clear_flag(m_hintLabel, LV_OBJ_FLAG_HIDDEN);
}

// ============================================================
// GameOver
// ============================================================

void FruitNinjaRenderer::showGameOver(lv_obj_t* parent, const FruitNinjaLogic& logic,
	lv_obj_t*& restartBtn, lv_obj_t*& backBtn)
{
	if (m_gameOverShown) return;
	m_gameOverShown = true;

	// 遮罩
	lv_obj_remove_flag(m_overlay, LV_OBJ_FLAG_HIDDEN);

	// 标题
	lv_obj_align(m_gameOverTitle, LV_ALIGN_CENTER, 0, -180);
	lv_obj_clear_flag(m_gameOverTitle, LV_OBJ_FLAG_HIDDEN);

	// 得分
	int pCount = logic.getPlayerCount();
	if (pCount >= 2)
	{
		lv_label_set_text_fmt(m_gameOverScore,
			"P1: %d  |  P2: %d", logic.getScore(0), logic.getScore(1));
	}
	else
	{
		lv_label_set_text_fmt(m_gameOverScore,
			"最终得分: %d", logic.getScore(0));
	}
	lv_obj_align(m_gameOverScore, LV_ALIGN_CENTER, 0, -100);
	lv_obj_clear_flag(m_gameOverScore, LV_OBJ_FLAG_HIDDEN);

	// 重新开始按钮
	restartBtn = lv_button_create(parent);
	lv_obj_set_size(restartBtn, 220, 64);
	lv_obj_set_style_radius(restartBtn, 16, 0);
	lv_obj_set_style_bg_color(restartBtn, lv_color_hex(0xff00c853), 0);
	lv_obj_set_style_bg_opa(restartBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(restartBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(restartBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(restartBtn, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(restartBtn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(restartBtn, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(restartBtn, 3, LV_STATE_FOCUSED);
	lv_obj_align(restartBtn, LV_ALIGN_CENTER, 0, 10);
	auto lblRestart = lv_label_create(restartBtn);
	lv_label_set_text(lblRestart, "重新开始");
	lv_obj_set_style_text_color(lblRestart, lv_color_hex(0xff000000), 0);
	lv_obj_center(lblRestart);

	// 返回菜单按钮
	backBtn = lv_button_create(parent);
	lv_obj_set_size(backBtn, 220, 64);
	lv_obj_set_style_radius(backBtn, 16, 0);
	lv_obj_set_style_bg_color(backBtn, lv_color_hex(0xff555566), 0);
	lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(backBtn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(backBtn, 4, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(backBtn, lv_color_hex(0xFFFFFFFF), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(backBtn, LV_OPA_60, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(backBtn, 3, LV_STATE_FOCUSED);
	lv_obj_align(backBtn, LV_ALIGN_CENTER, 0, 100);
	auto lblBack = lv_label_create(backBtn);
	lv_label_set_text(lblBack, "返回菜单");
	lv_obj_set_style_text_color(lblBack, lv_color_hex(0xffffffff), 0);
	lv_obj_center(lblBack);
}

// ============================================================
// 刀光线段 — 追加一条线段段（数据存储，绘制在 render() 中画进 canvas）
// ============================================================

void FruitNinjaRenderer::addTrailSegment(float x1, float y1, float x2, float y2)
{
	auto& s = m_segments[m_segWrite];
	s = TrailSegment{};
	s.x1 = x1; s.y1 = y1;
	s.x2 = x2; s.y2 = y2;
	s.opacity = 255;
	s.active = true;

	m_segWrite = (m_segWrite + 1) % kMaxSegments;
}

// ============================================================
// 白刃轨迹 — 老化（仅数据）
// ============================================================

void FruitNinjaRenderer::ageTrail(float dt)
{
	float fadeAmount = dt * TRAIL_FADE_RATE;
	uint8_t fadeStep = (fadeAmount > 255.0f) ? 255 : static_cast<uint8_t>(fadeAmount);
	if (fadeStep < 1) fadeStep = 1;

	for (int i = 0; i < kMaxSegments; i++)
	{
		auto& s = m_segments[i];
		if (!s.active) continue;
		if (s.opacity <= fadeStep) s.active = false;
		else s.opacity -= fadeStep;
	}
}

// ============================================================
// 白刃轨迹 — 清空（仅数据）
// ============================================================

void FruitNinjaRenderer::clearTrail()
{
	for (int i = 0; i < kMaxSegments; i++)
		m_segments[i].active = false;
	m_segWrite = 0;
}

// ============================================================
// canvas 像素绘制辅助（自绘，不经 LVGL）
// ============================================================

uint16_t FruitNinjaRenderer::blend565(uint16_t dst, uint16_t src, uint8_t opa)
{
	if (opa >= 255) return src;
	if (opa == 0) return dst;
	uint32_t dr = (dst >> 11) & 0x1F, dg = (dst >> 5) & 0x3F, db = dst & 0x1F;
	uint32_t sr = (src >> 11) & 0x1F, sg = (src >> 5) & 0x3F, sb = src & 0x1F;
	uint32_t inv = 255 - opa;
	uint32_t r = (sr * opa + dr * inv) / 255;
	uint32_t g = (sg * opa + dg * inv) / 255;
	uint32_t b = (sb * opa + db * inv) / 255;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

void FruitNinjaRenderer::fillRect(int x, int y, int w, int h, uint16_t color)
{
	if (!m_canvasBuf) return;
	const int W = FruitNinjaLogic::SCREEN_W;
	const int H = FruitNinjaLogic::SCREEN_H;
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w - 1; if (x1 >= W) x1 = W - 1;
	int y1 = y + h - 1; if (y1 >= H) y1 = H - 1;
	if (x0 > x1 || y0 > y1) return;

	// 首行逐像素填充，随后各行 memcpy 首行（PSRAM 下远快于逐像素写）
	uint8_t* base = m_canvasBuf + (size_t)y0 * m_canvasStride + (size_t)x0 * 2;
	uint16_t* row = reinterpret_cast<uint16_t*>(base);
	for (int xx = x0; xx <= x1; xx++) row[xx - x0] = color;
	size_t bytes = (size_t)(x1 - x0 + 1) * 2;
	for (int yy = y0 + 1; yy <= y1; yy++)
		memcpy(m_canvasBuf + (size_t)yy * m_canvasStride + (size_t)x0 * 2, base, bytes);
}

void FruitNinjaRenderer::fillCircle(int cx, int cy, int r, uint16_t color, uint8_t opa)
{
	if (!m_canvasBuf || r <= 0 || opa == 0) return;
	const int W = FruitNinjaLogic::SCREEN_W;
	const int H = FruitNinjaLogic::SCREEN_H;
	int x0 = cx - r, x1 = cx + r;
	int y0 = cy - r, y1 = cy + r;
	if (x1 < 0 || y1 < 0 || x0 >= W || y0 >= H) return;
	int r2 = r * r;
	for (int yy = y0; yy <= y1; yy++)
	{
		if (yy < 0 || yy >= H) continue;
		uint16_t* row = reinterpret_cast<uint16_t*>(m_canvasBuf + (size_t)yy * m_canvasStride);
		int dy = yy - cy;
		int half = (int)sqrtf((float)(r2 - dy * dy));
		int sx = cx - half; if (sx < 0) sx = 0;
		int ex = cx + half; if (ex >= W) ex = W - 1;
		for (int xx = sx; xx <= ex; xx++)
		{
			if (opa >= 255) row[xx] = color;
			else row[xx] = blend565(row[xx], color, opa);
		}
	}
}

void FruitNinjaRenderer::drawLine(int x1, int y1, int x2, int y2, int width, uint16_t color, uint8_t opa)
{
	if (!m_canvasBuf || width <= 0 || opa == 0) return;
	const int W = FruitNinjaLogic::SCREEN_W;
	const int H = FruitNinjaLogic::SCREEN_H;
	int dx = x2 - x1, dy = y2 - y1;
	float len = sqrtf((float)(dx * dx + dy * dy));
	if (len < 0.5f) { fillCircle(x1, y1, width / 2, color, opa); return; }
	float ux = dx / len, uy = dy / len;
	int hw = width / 2;
	int x0 = (x1 < x2 ? x1 : x2) - hw - 1, xA = (x1 > x2 ? x1 : x2) + hw + 1;
	int y0 = (y1 < y2 ? y1 : y2) - hw - 1, yB = (y1 > y2 ? y1 : y2) + hw + 1;
	if (xA < 0 || yB < 0 || x0 >= W || y0 >= H) return;
	float hw2 = (float)(hw * hw);
	for (int yy = y0; yy <= yB; yy++)
	{
		if (yy < 0 || yy >= H) continue;
		uint16_t* row = reinterpret_cast<uint16_t*>(m_canvasBuf + (size_t)yy * m_canvasStride);
		float vy = (float)(yy - y1);
		for (int xx = x0; xx <= xA; xx++)
		{
			if (xx < 0 || xx >= W) continue;
			float vx = (float)(xx - x1);
			float along = vx * ux + vy * uy;         // 沿线段方向投影
			if (along < -hw) along = -hw;             // 圆帽：两端放宽
			else if (along > len + hw) along = len + hw;
			float px = vx - along * ux;
			float py = vy - along * uy;
			if (px * px + py * py <= hw2)
			{
				if (opa >= 255) row[xx] = color;
				else row[xx] = blend565(row[xx], color, opa);
			}
		}
	}
}

// ============================================================
// 水果提示标签更新（LVGL 对象，画在 canvas 之上）
// ============================================================

void FruitNinjaRenderer::updateFruitLabels(const FruitNinjaLogic& logic, int fruitCount,
	const FruitNinjaLogic::Fruit* fruits, bool isPlaying)
{
	(void)logic;
	for (int i = 0; i < MAX_FRUITS; i++)
	{
		auto* label = m_fruitLabels[i];
		if (i < fruitCount && fruits[i].active && !fruits[i].sliced
			&& fruits[i].type != FruitNinjaLogic::FruitType::Bomb && isPlaying)
		{
			const auto& f = fruits[i];
			char buf[8];
			snprintf(buf, sizeof(buf), "%s%s",
				FruitNinjaLogic::dirToArrow(f.requiredDir),
				FruitNinjaLogic::btnToChar(f.requiredBtn));
			if (strcmp(m_fruitLabelText[i], buf) != 0)
			{
				strcpy(m_fruitLabelText[i], buf);
				lv_label_set_text(label, buf);
			}
			float r = FruitNinjaLogic::getFruitRadius(f.type);
			lv_obj_set_pos(label, (int)(f.x - 16), (int)(f.y - r - 24));
			lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

// ============================================================
// 析构：释放 canvas PSRAM 缓冲
// ============================================================

FruitNinjaRenderer::~FruitNinjaRenderer()
{
	if (m_canvasBuf)
	{
		heap_caps_free(m_canvasBuf);
		m_canvasBuf = nullptr;
	}
}
