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

	ESP_LOGI(TAG, "对象池创建完成: %d fruits, %d particles", MAX_FRUITS, MAX_PARTICLES);
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

	// ── HUD ──
	updateHUD(lv_obj_get_parent(m_scoreLabel), logic);
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
			lv_label_set_text_fmt(m_livesLabel, "\u2764 P1:%d  P2:%d",
				logic.getLives(0), logic.getLives(1));
		}
		else
		{
			lv_label_set_text_fmt(m_livesLabel, "\u2764 x%d", logic.getLives(0));
		}
		lv_obj_align(m_livesLabel, LV_ALIGN_TOP_RIGHT, -12, 8);
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
		"\u2190\u2191\u2192\u2193 + A/B/X/Y \u2261  \u21bb \u89e6\u5c4f\u70b9\u6309");
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
