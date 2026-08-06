#pragma once

#include "lvgl.h"
#include "fruitNinjaLogic.hpp"

/**
 * @brief 水果忍者 LVGL 渲染器
 *
 * 对象池模式：预分配所有 LVGL 对象，每帧只更新位置/显隐。
 *
 * 白刃轨迹（线段池）：触摸每帧产生一条线段段 (上一帧位置, 当前帧位置)，
 * 每段独立 lv_line 叠加光晕/刃芯，段间断开、每段独立超时老化淡出。
 * 渲染层不关心手指 ID/槽位（GT911 ID 会复用，不可用于 UI 归属）。
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

	/** @brief 显示 GameOver 界面 */
	void showGameOver(lv_obj_t* parent, const FruitNinjaLogic& logic,
		lv_obj_t*& restartBtn, lv_obj_t*& backBtn);

	/** @brief 显示得分/生命/计时标签 */
	void updateHUD(lv_obj_t* parent, const FruitNinjaLogic& logic);

	/** @brief 获取退出按钮（供 App 注册回调） */
	lv_obj_t* getExitBtn() const { return m_exitBtn; }

	// ── 白刃轨迹接口 ──
	// 线段池模型：触摸每帧把 (上一帧位置, 当前帧位置) 作为一条线段段追加，
	// 段与段天然断开（不同点击不粘连）、每段独立超时老化（不被新段续命）。
	// 渲染层完全不关心手指 ID/槽位（GT911 ID 会复用，不可用于 UI 归属）。

	/** @brief 追加一条刀光线段段（触摸移动产生） */
	void addTrailSegment(float x1, float y1, float x2, float y2);

	/** @brief 每帧统一老化所有线段段（逐段独立超时渐隐清理） */
	void ageTrail(float dt);

	/** @brief 一键清空所有线段段 */
	void clearTrail();

private:
	static constexpr int MAX_FRUITS = FruitNinjaLogic::MAX_FRUITS;
	static constexpr int MAX_PARTICLES = FruitNinjaLogic::MAX_PARTICLES;

	// ── 刀光线段池（每段 2 点、独立 lv_line，段间断开） ──
	struct TrailSegment
	{
		float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
		uint8_t opacity = 255;   // 段透明度（独立超时老化，互不影响）
		bool active = false;
	};

	// 容量依据：残影约 1s（TRAIL_FADE_RATE=250）。最坏 60fps 双人各 1 指 ≈ 120 段/s，
	// 128 段可完整保留 ~1s 拖尾（30fps 双指 ≈ 2s+），覆盖时最旧段已近消失，余量充足。
	static constexpr int kMaxSegments = 128;     // 线段池容量（环形覆盖最旧段）
	static constexpr float TRAIL_FADE_RATE = 250.0f;   // 统一超时老化速率（~1s 渐隐清理）

	TrailSegment m_segments[kMaxSegments]{};
	int m_segWrite = 0;          // 环形写指针

	/** @brief 更新某段显隐/透明度；opacity 归零时隐藏 */
	void updateSegment(int index);

	// ── 白刃 LVGL 对象（每段 2 层叠加：光晕 + 刃芯，替代 shadow 避免白框） ──
	lv_obj_t* m_segGlow[kMaxSegments]{};   // 光晕（半透白 14px）
	lv_obj_t* m_segCore[kMaxSegments]{};   // 刃芯（白色 4px）
	// 每段点阵缓冲（lv_line_set_points 持有引用，须为常驻成员）
	lv_point_precise_t m_segPts[kMaxSegments][2]{};

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
