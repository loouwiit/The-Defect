#pragma once

#include "lvgl.h"
#include "fruitNinjaLogic.hpp"

/**
 * @brief 水果忍者 LVGL 渲染器
 *
 * 对象池模式：预分配所有 LVGL 对象，每帧只更新位置/显隐。
 *
 * 白刃轨迹：使用双 lv_line（主刃线+发光层）叠加实现辉光效果。
 * 触摸回调通过 addTrailPoint() 写入环形缓冲，每帧 ageTrail() 老化淡出。
 */
class FruitNinjaRenderer
{
public:
	FruitNinjaRenderer() = default;
	~FruitNinjaRenderer() = default;

	/** @brief 创建对象池 */
	void create(lv_obj_t* parent);

	/** @brief 每帧渲染：同步逻辑状态到 LVGL 对象 + 更新白刃轨迹 */
	void render(const FruitNinjaLogic& logic);

	/** @brief 显示/隐藏 GameOver 界面 */
	void showGameOver(lv_obj_t* parent, const FruitNinjaLogic& logic,
		lv_obj_t*& restartBtn, lv_obj_t*& backBtn);
	void hideGameOver();

	/** @brief 显示得分/生命/计时标签 */
	void updateHUD(lv_obj_t* parent, const FruitNinjaLogic& logic);

	/** @brief 获取退出按钮（供 App 注册回调） */
	lv_obj_t* getExitBtn() const { return m_exitBtn; }

	// ── 白刃轨迹接口 ──

	/** @brief 触摸开始时记录首个轨迹点 */
	void handleTouchStart(float x, float y);

	/** @brief 添加轨迹点到环形缓冲头部 */
	void addTrailPoint(float x, float y);

	/** @brief 触摸结束 */
	void handleTouchEnd();

	/** @brief 每帧老化轨迹点（触摸中慢衰，释放后快衰） */
	void ageTrail(float dt);

	/** @brief 标记轨迹发生变化，下次 updateBladeTrail 会重建 lv_line 点阵 */
	void markTrailDirty() { m_trailDirty = true; }

	/** @brief 一键清空所有轨迹点 */
	void clearTrail();

private:
	static constexpr int MAX_FRUITS = FruitNinjaLogic::MAX_FRUITS;
	static constexpr int MAX_PARTICLES = FruitNinjaLogic::MAX_PARTICLES;

	// ── 白刃轨迹点 ──
	struct TrailPoint
	{
		float x = 0, y = 0;
		uint8_t opacity = 255; // 255=最亮, 0=完全透明
	};

	static constexpr int MAX_TRAIL_POINTS = 32;
	static constexpr float TRAIL_FADE_ACTIVE = 0.0f;      // 触摸中不老化（环形缓冲自然淘汰）
	static constexpr float TRAIL_FADE_RELEASE = 600.0f;   // 释放后 ~400ms 完全淡出

	TrailPoint m_trailBuffer[MAX_TRAIL_POINTS]{};
	int m_trailHead = 0;
	int m_trailCount = 0;
	bool m_isTouching = false;
	bool m_trailDirty = false;  // 脏标记，避免每帧重建 lv_line 点阵

	/** @brief 更新白刃线（从 trailBuffer 构建 lv_line 点阵） */
	void updateBladeTrail();

	// ── 白刃 LVGL 对象（三层叠加，替代 shadow 避免白框） ──
	lv_obj_t* m_bladeLine{};   // 外层光晕（半透白 24px）
	lv_obj_t* m_bladeGlow{};   // 中层发光（半透白 14px）
	lv_obj_t* m_bladeCore{};   // 刃芯（白色 4px）

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

	// ── 退出按钮 ──
	lv_obj_t* m_exitBtn{};

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
