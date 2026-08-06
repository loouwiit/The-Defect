#pragma once

#include "lvgl.h"
#include "fruitNinjaLogic.hpp"

/**
 * @brief 水果忍者 LVGL 渲染器（canvas 版）
 *
 * 动态层（水果/粒子/刀光/分隔线）直接画进一块全屏 PSRAM RGB565 canvas，
 * 每帧对 canvas 做一次失效。HUD/水果提示标签/退出按钮/GameOver 保留 LVGL 对象
 * （对象树大幅缩小后，其逐帧更新开销可忽略）。
 *
 * 白刃轨迹（线段池）：触摸每帧产生一条线段段 (上一帧位置, 当前帧位置)，
 * 段数据存入池中，每帧由 render() 直接画进 canvas（光晕低透明度粗线 + 刃芯实线），
 * 逐段独立超时老化淡出。渲染层不关心手指 ID/槽位（GT911 ID 会复用）。
 */
class FruitNinjaRenderer
{
public:
	FruitNinjaRenderer() = default;
	~FruitNinjaRenderer();              // 释放 canvas PSRAM 缓冲

	/** @brief 创建 canvas + HUD 对象池 */
	void create(lv_obj_t* parent);

	/** @brief 每帧渲染：画动态层进 canvas + 更新 HUD/提示标签 */
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

	// ── 刀光线段池（数据；绘制在 render() 中画进 canvas） ──
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

	// ── 动态层 canvas（全屏 PSRAM RGB565，1280×720） ──
	lv_obj_t* m_canvas{};
	uint8_t* m_canvasBuf{};      // heap_caps_malloc(SPIRAM)，析构时释放
	lv_draw_buf_t* m_canvasDrawBuf{};
	int m_canvasStride = 0;      // 每行字节数（含对齐 padding）

	// ── LVGL 对象（画在 canvas 之上） ──
	// 水果提示标签（↑A 等）
	lv_obj_t* m_fruitLabels[MAX_FRUITS]{};
	// HUD
	lv_obj_t* m_scoreLabel{};
	lv_obj_t* m_livesLabel{};
	lv_obj_t* m_timerLabel{};
	lv_obj_t* m_hintLabel{};
	// 退出按钮
	lv_obj_t* m_exitBtn{};
	// GameOver
	lv_obj_t* m_overlay{};
	lv_obj_t* m_gameOverTitle{};
	lv_obj_t* m_gameOverScore{};
	bool m_gameOverShown = false;

	// ── 渲染缓存（提示标签文本 / HUD 值 / 上一帧位置） ──
	char m_fruitLabelText[MAX_FRUITS][8]{};
	int m_lastScore0 = -1, m_lastScore1 = -1;
	int m_lastLives0 = -1, m_lastLives1 = -1;
	int m_lastTimerSec = -1;
	int8_t m_lastPCount = -1;
	bool m_hintSet = false;
	// 上一帧位置（脏矩形计算用：只清/画/失效变化区域，避免整屏重绘）
	int m_prevFruitX[MAX_FRUITS]{}, m_prevFruitY[MAX_FRUITS]{}, m_prevFruitR[MAX_FRUITS]{};
	bool m_prevFruitActive[MAX_FRUITS]{};
	int m_prevParticleX[MAX_PARTICLES]{}, m_prevParticleY[MAX_PARTICLES]{};
	bool m_prevParticleActive[MAX_PARTICLES]{};
	bool m_prevSegActive[kMaxSegments]{};

	// ── canvas 像素绘制辅助（自绘，不经 LVGL） ──
	static uint16_t blend565(uint16_t dst, uint16_t src, uint8_t opa);
	void fillRect(int x, int y, int w, int h, uint16_t color);
	void fillCircle(int cx, int cy, int r, uint16_t color, uint8_t opa);
	void drawLine(int x1, int y1, int x2, int y2, int width, uint16_t color, uint8_t opa);
	void updateFruitLabels(const FruitNinjaLogic& logic, int fruitCount,
		const FruitNinjaLogic::Fruit* fruits, bool isPlaying);

	// 颜色配置（水果纯色圆）
	struct FruitColors { uint32_t fill; };
	static constexpr FruitColors COLORS[] = {
		{ 0xff00e676 }, // Watermelon - green
		{ 0xffff5252 }, // Apple - red
		{ 0xffffa726 }, // Orange - orange
		{ 0xffeeff41 }, // Banana - yellow
		{ 0xff555555 }, // Bomb - dark gray
	};
};
