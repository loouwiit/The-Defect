#pragma once

#include "lvgl.h"
#include "fruitNinjaLogic.hpp"

/**
 * @brief 水果忍者 LVGL 渲染器
 *
 * 对象池模式：预分配所有 LVGL 对象，每帧只更新位置/显隐。
 */
class FruitNinjaRenderer
{
public:
	FruitNinjaRenderer() = default;
	~FruitNinjaRenderer() = default;

	/** @brief 创建对象池 */
	void create(lv_obj_t* parent);

	/** @brief 每帧渲染：同步逻辑状态到 LVGL 对象 */
	void render(const FruitNinjaLogic& logic);

	/** @brief 显示/隐藏 GameOver 界面 */
	void showGameOver(lv_obj_t* parent, const FruitNinjaLogic& logic,
		lv_obj_t*& restartBtn, lv_obj_t*& backBtn);
	void hideGameOver();

	/** @brief 显示得分/生命/计时标签 */
	void updateHUD(lv_obj_t* parent, const FruitNinjaLogic& logic);

private:
	static constexpr int MAX_FRUITS = FruitNinjaLogic::MAX_FRUITS;
	static constexpr int MAX_PARTICLES = FruitNinjaLogic::MAX_PARTICLES;

	// ── 对象池 ──
	// 水果图片（lv_image，带 bg_color 回退）
	lv_obj_t* m_fruitImages[MAX_FRUITS]{};
	// 水果提示标签（↑A 等）
	lv_obj_t* m_fruitLabels[MAX_FRUITS]{};
	// 粒子（小圆形）
	lv_obj_t* m_particles[MAX_PARTICLES]{};
	// 分隔线（2P 模式）
	lv_obj_t* m_divider{};
	// HUD
	lv_obj_t* m_scoreLabel{};
	lv_obj_t* m_livesLabel{};
	lv_obj_t* m_timerLabel{};
	lv_obj_t* m_hintLabel{};

	// ── GameOver ──
	lv_obj_t* m_overlay{};
	lv_obj_t* m_gameOverTitle{};
	lv_obj_t* m_gameOverScore{};
	bool m_gameOverShown = false;

	// 颜色配置（图片不存在的回退）
	struct FruitColors { uint32_t fill; };
	static constexpr FruitColors COLORS[] = {
		{ 0xff00e676 }, // Watermelon - green
		{ 0xffff5252 }, // Apple - red
		{ 0xffffa726 }, // Orange - orange
		{ 0xffeeff41 }, // Banana - yellow
		{ 0xff555555 }, // Bomb - dark gray
	};

	/** @brief 图片 VFS 路径映射（按 FruitType 索引） */
	static constexpr const char* IMAGE_PATHS[] = {
		"F:system/fruitNinja/watermelon.jpg",
		"F:system/fruitNinja/apple.jpg",
		"F:system/fruitNinja/orange.jpg",
		"F:system/fruitNinja/banana.jpg",
		"F:system/fruitNinja/bomb.jpg",
	};
};
