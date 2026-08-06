#include "fruitNinjaRenderer.hpp"
#include "display/font.hpp"
#include "esp_log.h"
#include <cstring>

static constexpr char TAG[] = "FruitNinjaRenderer";

constexpr FruitNinjaRenderer::FruitColors FruitNinjaRenderer::COLORS[];
constexpr const char* FruitNinjaRenderer::IMAGE_PATHS[];

// ============================================================
// 创建对象池
// ============================================================

void FruitNinjaRenderer::create(lv_obj_t* parent)
{
	// ── 水果图片对象池 ──
	for (int i = 0; i < MAX_FRUITS; i++)
	{
		// lv_image 作为主渲染，支持 JPEG/PNG，bg_color 作回退
		m_fruitImages[i] = lv_image_create(parent);
		lv_obj_set_size(m_fruitImages[i], 80, 80);
		lv_obj_set_style_radius(m_fruitImages[i], 40, 0);
		lv_obj_set_style_border_width(m_fruitImages[i], 0, 0);
		lv_obj_set_style_bg_opa(m_fruitImages[i], LV_OPA_COVER, 0);
		lv_obj_set_style_bg_color(m_fruitImages[i], lv_color_hex(0xffff00ff), 0);
		lv_obj_set_style_shadow_width(m_fruitImages[i], 8, 0);
		lv_obj_set_style_shadow_opa(m_fruitImages[i], LV_OPA_30, 0);
		lv_obj_remove_flag(m_fruitImages[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_fruitImages[i], LV_OBJ_FLAG_HIDDEN);

		// 提示标签
		m_fruitLabels[i] = lv_label_create(parent);
		lv_label_set_text(m_fruitLabels[i], "");
		lv_obj_set_style_text_font(m_fruitLabels[i],
			FontLoader::getDefault(FontLoader::FontSize::Small), 0);
		lv_obj_set_style_text_color(m_fruitLabels[i], lv_color_hex(0xffffffff), 0);
		lv_obj_remove_flag(m_fruitLabels[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_fruitLabels[i], LV_OBJ_FLAG_HIDDEN);
	}

	// ── 粒子对象池 ──
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		m_particles[i] = lv_obj_create(parent);
		lv_obj_set_style_border_width(m_particles[i], 0, 0);
		lv_obj_set_style_bg_opa(m_particles[i], LV_OPA_COVER, 0);
		lv_obj_remove_flag(m_particles[i], LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_particles[i], LV_OBJ_FLAG_HIDDEN);
	}

	// ── 2P 分隔线 ──
	m_divider = lv_obj_create(parent);
	lv_obj_set_size(m_divider, 2, FruitNinjaLogic::SCREEN_H);
	lv_obj_set_pos(m_divider, FruitNinjaLogic::SCREEN_W / 2 - 1, 0);
	lv_obj_add_flag(m_divider, LV_OBJ_FLAG_HIDDEN);
	lv_obj_set_style_bg_color(m_divider, lv_color_hex(0x44ffffff), 0);
	lv_obj_set_style_bg_opa(m_divider, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(m_divider, 0, 0);
	lv_obj_remove_flag(m_divider, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_add_flag(m_divider, LV_OBJ_FLAG_HIDDEN);

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

	// ── 白刃轨迹线（每 trail 三层叠加，不用 shadow 避免矩形白框） ──
	const lv_coord_t bladeWidths[3] = { 24, 14, 4 };   // 外层光晕 / 中层发光 / 刃芯
	const lv_color_t bladeColors[3] = {
		lv_color_hex(0x18ffffff),
		lv_color_hex(0x44ffffff),
		lv_color_hex(0xccffffff),
	};
	for (int t = 0; t < MAX_TRAILS; t++)
	{
		for (int layer = 0; layer < 3; layer++)
		{
			auto line = lv_line_create(parent);
			lv_obj_set_style_line_width(line, bladeWidths[layer], 0);
			lv_obj_set_style_line_color(line, bladeColors[layer], 0);
			lv_obj_set_style_line_rounded(line, true, 0);
			lv_obj_set_style_bg_opa(line, LV_OPA_TRANSP, 0);
			lv_obj_set_style_border_width(line, 0, 0);
			lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
			m_blades[t][layer] = line;
		}
	}

	ESP_LOGI(TAG, "对象池创建完成: %d fruits, %d particles, blade trail", MAX_FRUITS, MAX_PARTICLES);
}

// ============================================================
// 每帧渲染
// ============================================================

void FruitNinjaRenderer::render(const FruitNinjaLogic& logic)
{
	auto state = logic.getState();
	bool isPlaying = (state == FruitNinjaLogic::State::Playing);

	// 更新分隔线
	if (logic.getPlayerCount() >= 2)
		lv_obj_clear_flag(m_divider, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(m_divider, LV_OBJ_FLAG_HIDDEN);

	// ── 更新水果 ──
	int fruitCount = logic.getFruitCount();
	const auto* fruits = logic.getFruits();

	for (int i = 0; i < MAX_FRUITS; i++)
	{
		if (i < fruitCount && fruits[i].active)
		{
			const auto& f = fruits[i];
			float r = FruitNinjaLogic::getFruitRadius(f.type);
			int diam = static_cast<int>(r * 2);
			int posX = static_cast<int>(f.x - r);
			int posY = static_cast<int>(f.y - r);

			// ── 水果图片 ──
			auto* img = m_fruitImages[i];
			lv_obj_set_size(img, diam, diam);
			lv_obj_set_pos(img, posX, posY);
			lv_obj_set_style_radius(img, static_cast<int32_t>(r), 0);

			// 设置图片源（不存在时图片透明，下方 bg_color 显示）
			int typeIdx = static_cast<int>(f.type);
			if (typeIdx >= 0 && typeIdx < 5) {
				lv_image_set_src(img, IMAGE_PATHS[typeIdx]);
			}

			// 回退颜色（图片不存在时显示为纯色圆）
			uint32_t color = COLORS[typeIdx].fill;
			lv_obj_set_style_bg_color(img, lv_color_hex(color), 0);
			lv_obj_set_style_shadow_color(img, lv_color_hex(color), 0);

			if (f.sliced)
			{
				// 已切片：淡出
				lv_obj_set_style_opa(img, f.opacity, 0);
				lv_obj_set_size(img, diam / 2, diam / 2);
			}
			else
			{
				lv_obj_set_style_opa(img, LV_OPA_COVER, 0);
			}
			lv_obj_clear_flag(img, LV_OBJ_FLAG_HIDDEN);

			// ── 提示标签 ──
			auto* label = m_fruitLabels[i];
			if (!f.sliced && f.type != FruitNinjaLogic::FruitType::Bomb && isPlaying)
			{
				char buf[8];
				snprintf(buf, sizeof(buf), "%s%s",
					FruitNinjaLogic::dirToArrow(f.requiredDir),
					FruitNinjaLogic::btnToChar(f.requiredBtn));
				lv_label_set_text(label, buf);
				lv_obj_set_pos(label,
					static_cast<int>(f.x - 16),
					static_cast<int>(f.y - r - 24));
				lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
			}
		}
		else
		{
			lv_obj_add_flag(m_fruitImages[i], LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(m_fruitLabels[i], LV_OBJ_FLAG_HIDDEN);
		}
	}

	// ── 更新粒子 ──
	int particleCount = logic.getParticleCount();
	const auto* particles = logic.getParticles();

	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		if (i < particleCount && particles[i].active)
		{
			const auto& p = particles[i];
			auto* obj = m_particles[i];
			int sz = p.size;
			lv_obj_set_size(obj, sz, sz);
			lv_obj_set_pos(obj, static_cast<int>(p.x - sz / 2),
				static_cast<int>(p.y - sz / 2));
			lv_obj_set_style_radius(obj, sz / 2, 0);
			lv_obj_set_style_bg_color(obj, lv_color_make(p.r, p.g, p.b), 0);
			lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
			lv_obj_set_style_opa(obj, p.opacity, 0);
			lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_particles[i], LV_OBJ_FLAG_HIDDEN);
		}
	}

	// ── 退出按钮（游戏中显示，GameOver 时隐藏） ──
	if (isPlaying)
		lv_obj_clear_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);

	// ── HUD ──
	updateHUD(lv_obj_get_parent(m_scoreLabel), logic);

	// ── 白刃轨迹 ──
	updateBladeTrail();
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

	// 分数
	int pCount = logic.getPlayerCount();
	if (pCount >= 2)
	{
		lv_label_set_text_fmt(m_scoreLabel, "P1:%d  P2:%d",
			logic.getScore(0), logic.getScore(1));
	}
	else
	{
		lv_label_set_text_fmt(m_scoreLabel, "得分: %d", logic.getScore(0));
	}
	lv_obj_align(m_scoreLabel, LV_ALIGN_TOP_LEFT, 12, 8);
	lv_obj_clear_flag(m_scoreLabel, LV_OBJ_FLAG_HIDDEN);

	// 生命 / 计时
	if (logic.getMode() == FruitNinjaLogic::GameMode::Classic)
	{
		if (pCount >= 2)
		{
			lv_label_set_text_fmt(m_livesLabel, "生命 P1:%d  P2:%d",
				logic.getLives(0), logic.getLives(1));
		}
		else
		{
			lv_label_set_text_fmt(m_livesLabel, "生命 x%d", logic.getLives(0));
		}
		lv_obj_align_to(m_livesLabel, m_scoreLabel, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
		lv_obj_clear_flag(m_livesLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_timerLabel, LV_OBJ_FLAG_HIDDEN);
	}
	else // Arcade
	{
		int sec = static_cast<int>(logic.getTimeRemaining());
		lv_label_set_text_fmt(m_timerLabel, "%02d", sec);
		lv_obj_align(m_timerLabel, LV_ALIGN_TOP_MID, 0, 8);
		lv_obj_clear_flag(m_timerLabel, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(m_livesLabel, LV_OBJ_FLAG_HIDDEN);

		// 倒计时快结束时变红
		if (sec <= 10)
			lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(0xffff5252), 0);
		else
			lv_obj_set_style_text_color(m_timerLabel, lv_color_hex(0xffffa726), 0);
	}

	// 底部操作提示
	lv_label_set_text(m_hintLabel,
		"\u2190\u2191\u2192\u2193 + A/B/X/Y \u2261  \u21bb \u89e6\u5c4f\u5212\u52a8");
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
			"\u6700\u7ec8\u5f97\u5206: %d", logic.getScore(0));
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
	lv_label_set_text(lblRestart, "\u91cd\u65b0\u5f00\u59cb");
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
	lv_label_set_text(lblBack, "\u8fd4\u56de\u83dc\u5355");
	lv_obj_set_style_text_color(lblBack, lv_color_hex(0xffffffff), 0);
	lv_obj_center(lblBack);
}

void FruitNinjaRenderer::hideGameOver()
{
	m_gameOverShown = false;
	lv_obj_add_flag(m_overlay, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_gameOverTitle, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_flag(m_gameOverScore, LV_OBJ_FLAG_HIDDEN);
	// restart/back buttons are deleted/destroyed by the caller
}

// ============================================================
// 白刃轨迹 — 触摸开始
// ============================================================

void FruitNinjaRenderer::handleTouchStart(int trail, float x, float y)
{
	if (trail < 0 || trail >= MAX_TRAILS) return;
	m_trails[trail].active = true;
	clearTrail(trail);
	addTrailPoint(trail, x, y);
}

// ============================================================
// 白刃轨迹 — 添加点
// ============================================================

void FruitNinjaRenderer::addTrailPoint(int trail, float x, float y)
{
	if (trail < 0 || trail >= MAX_TRAILS) return;
	auto& tr = m_trails[trail];

	if (tr.count >= MAX_TRAIL_POINTS)
	{
		// 环形缓冲满时覆盖最旧的点
		tr.head = (tr.head + 1) % MAX_TRAIL_POINTS;
	}
	else
	{
		tr.head = tr.count;
		tr.count++;
	}

	auto& pt = tr.buf[tr.head];
	pt.x = x;
	pt.y = y;
	pt.opacity = 255;

	m_trailDirty = true;
}

// ============================================================
// 白刃轨迹 — 触摸结束
// ============================================================

void FruitNinjaRenderer::handleTouchEnd(int trail)
{
	if (trail < 0 || trail >= MAX_TRAILS) return;
	m_trails[trail].active = false;
}

// ============================================================
// 白刃轨迹 — 老化（所有 trail）
// ============================================================

void FruitNinjaRenderer::ageTrail(float dt)
{
	for (int t = 0; t < MAX_TRAILS; t++)
	{
		auto& tr = m_trails[t];
		if (tr.count == 0) continue;

		float fadeRate = tr.active ? TRAIL_FADE_ACTIVE : TRAIL_FADE_RELEASE;
		if (fadeRate <= 0.0f) continue; // 触摸中不老化

		float fadeAmount = dt * fadeRate;
		uint8_t fadeStep = (fadeAmount > 255.0f) ? 255 : static_cast<uint8_t>(fadeAmount);
		if (fadeStep < 1) fadeStep = 1;

		bool changed = false;
		int newCount = 0;
		for (int i = 0; i < tr.count; i++)
		{
			auto& pt = tr.buf[i];
			if (pt.opacity <= fadeStep)
			{
				pt.opacity = 0;
				changed = true;
			}
			else
			{
				pt.opacity -= fadeStep;
				if (i != newCount)
					tr.buf[newCount] = pt;
				newCount++;
			}
		}
		if (newCount != tr.count)
		{
			tr.count = newCount;
			changed = true;
		}
		if (changed)
			m_trailDirty = true;
	}
}

// ============================================================
// 白刃轨迹 — 清空（指定 trail）
// ============================================================

void FruitNinjaRenderer::clearTrail(int trail)
{
	if (trail < 0 || trail >= MAX_TRAILS) return;
	m_trails[trail].count = 0;
	m_trails[trail].head = 0;
}

// ============================================================
// 白刃轨迹 — 更新 LVGL 线对象（所有 trail）
// ============================================================

void FruitNinjaRenderer::updateBladeTrail()
{
	static lv_point_precise_t pts[MAX_TRAIL_POINTS];

	for (int t = 0; t < MAX_TRAILS; t++)
	{
		auto& tr = m_trails[t];

		if (tr.count < 2)
		{
			for (int layer = 0; layer < 3; layer++)
				lv_obj_add_flag(m_blades[t][layer], LV_OBJ_FLAG_HIDDEN);
			continue;
		}

		// 脏标记优化：只有轨迹点变化时才重建 lv_line 点阵
		if (m_trailDirty)
		{
			for (int i = 0; i < tr.count; i++)
			{
				pts[i].x = static_cast<lv_value_precise_t>(tr.buf[i].x);
				pts[i].y = static_cast<lv_value_precise_t>(tr.buf[i].y);
			}

			// 三层线共享同一组点
			for (int layer = 0; layer < 3; layer++)
				lv_line_set_points(m_blades[t][layer], pts, tr.count);
		}

		// 每帧更新透明度（渐隐动画需要）
		uint8_t oldestOpa = tr.buf[0].opacity;
		lv_obj_set_style_opa(m_blades[t][2], oldestOpa, 0);        // 刃芯
		lv_obj_set_style_opa(m_blades[t][0], oldestOpa * 2 / 3, 0); // 外层光晕
		lv_obj_set_style_opa(m_blades[t][1], oldestOpa / 3, 0);     // 中层发光

		for (int layer = 0; layer < 3; layer++)
			lv_obj_clear_flag(m_blades[t][layer], LV_OBJ_FLAG_HIDDEN);
	}

	m_trailDirty = false;
}
