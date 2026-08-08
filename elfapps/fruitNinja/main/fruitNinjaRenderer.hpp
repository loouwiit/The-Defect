#pragma once

#include "lvgl.h"
#include "fruitNinjaLogic.hpp"

/**
 * @brief 水果忍者 LVGL 渲染器（canvas 化重构）
 *
 * 游戏层（水果 + 粒子 + 分隔线 + 刀光）画进单个 lv_canvas（PSRAM RGB565 缓冲），
 * 软件像素绘制 + 脏区集合失效：
 *   - 每帧收集变化对象的脏区集合 D（上帧bbox ∪ 本帧bbox，精确不合并）
 *   - 相交扩散：与 D 传播相交的对象加入重画集 R（防止 clear 后残影 / 误伤覆盖对象）
 *   - 清 D 各区域背景 → 按 z 序重画 R → 对 D 逐区域 lv_obj_invalidate_area
 *   - LVGL 内置 lv_refr_join_area 自动合并相邻区域，分散区域保持精确小面积
 *
 * 要点：
 *   - 直接操作 canvas buffer 指针（纯内存写），绝不使用 lv_canvas_set_px（逐像素 invalidate）
 *   - 移除全部 shadow（模糊重算是性能杀手）
 *   - 刀光：段池 + 超时消失（TRAIL_LIFETIME 秒后瞬间消失），纯色不透明单层粗线（无淡出/半透明）
 *   - HUD / 退出按钮 / GameOver / 水果提示标签 保留 LVGL 对象（低频/轻量）
 *   - 所有方法必须在 Display::LockGuard 保护下调用
 */
class FruitNinjaRenderer
{
public:
	FruitNinjaRenderer() = default;
	~FruitNinjaRenderer();

	/** @brief 创建对象池（canvas 最底 + HUD/提示标签） */
	void create(lv_obj_t* parent);

	/** @brief 帧前计算（无锁）：收集脏区 + 重画集；必须在本帧 render() 前调用 */
	void beginFrame(const FruitNinjaLogic& logic);

	/** @brief 持锁提交：清脏区 → 按 z 序重画 → 逐区域失效 + HUD/按钮 */
	void render(const FruitNinjaLogic& logic);

	/** @brief 显示 GameOver 界面 */
	void showGameOver(lv_obj_t* parent, const FruitNinjaLogic& logic,
		lv_obj_t*& restartBtn, lv_obj_t*& backBtn);

	/** @brief 显示得分/生命/计时标签 */
	void updateHUD(lv_obj_t* parent, const FruitNinjaLogic& logic);

	/** @brief 获取退出按钮（供 App 注册回调） */
	lv_obj_t* getExitBtn() const { return m_exitBtn; }

	// ── 白刃轨迹接口 ──
	// 线段池模型：触摸每帧把 (上一帧位置, 当前帧位置) 作为一条线段段追加。
	// 段池 + 超时消失：每段保持 TRAIL_LIFETIME 秒后消失（无渐隐，无每帧重绘）。

	/** @brief 追加一条刀光线段段（触摸移动产生） */
	void addTrailSegment(float x1, float y1, float x2, float y2);

	/** @brief 每帧统一老化（递减各段剩余生命，超时后消失） */
	void ageTrail(float dt);

	/** @brief 一键清空所有线段段（保留上帧标记，下一帧 render 清除 canvas 残留） */
	void clearTrail();

private:
	static constexpr int W = 1280;
	static constexpr int H = 720;
	static constexpr int MaxFruits = FruitNinjaLogic::MaxFruits;
	static constexpr int MaxParticles = FruitNinjaLogic::MaxParticles;

	// 刀光段池 + 超时不透明：段保持 TRAIL_LIFETIME 秒后瞬间消失（无逐帧淡出，性能最优）
	// 64 段：60fps 持续划线 0.5s ≈ 30 段，池够大不顶掉；触摸停止后从尾部逐段消失
	static constexpr int kMaxSegments = 64;
	static constexpr float kMaxSegLen = 40.0f;   // 单段最大长度（px）：更长的线段在 addTrailSegment 拆分为多段
	static constexpr float TRAIL_LIFETIME = 0.5f;   // 段超时（秒）

	// ── canvas ──
	lv_obj_t* m_canvas{};
	uint8_t* m_buf{};           // PSRAM RGB565, W*H*2
	size_t m_stride = W * 2;    // RGB565 stride (bytes)
	uint16_t m_bg565{};         // 背景色（RGB565）
	bool m_initialInvalidated = false;
	bool m_dividerPainted = false;   // 2P 分割线是否已初始绘制（首帧强制标脏后置位）

	// ── 对象上帧状态（脏区收集用） ──
	struct BBox
	{
		bool active = false;
		int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	};
	BBox m_prevFruit[MaxFruits];
	BBox m_prevParticle[MaxParticles];

	// ── 刀光段（超时不透明：lifetime 到期后瞬间消失，无逐帧淡出/半透明） ──
	struct TrailSegment
	{
		float x1 = 0, y1 = 0, x2 = 0, y2 = 0;
		float life = 0;        // 剩余生命（秒），ageTrail 递减，<=0 消失
		bool active = false;
	};
	TrailSegment m_segments[kMaxSegments]{};
	int m_segWrite = 0;
	BBox m_prevSeg[kMaxSegments];

	// ── 脏区集合（每帧重建） ──
	lv_area_t m_dirty[128];
	int m_dirtyCount = 0;
	void dirtyClear() { m_dirtyCount = 0; }
	void dirtyAdd(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
	bool dirtyIntersects(const BBox& b) const;

	// ── 重画集（与脏区集合 D 相交筛选结果；仅一次遍历，无传播 O(n²)） ──
	bool m_redrawFruit[MaxFruits]{};
	bool m_redrawParticle[MaxParticles]{};
	bool m_redrawSeg[kMaxSegments]{};
	bool m_redrawDivider = false;

	// ── 软件绘制（直接写 m_buf） ──
	static uint16_t color565(uint32_t rgb);          // 0x00RRGGBB → RGB565
	static uint16_t color565rgb(uint8_t r, uint8_t g, uint8_t b);
	static uint16_t blend565(uint16_t dst, uint16_t src, lv_opa_t opa);
	void fillRect(int x1, int y1, int x2, int y2, uint16_t c);
	void fillCircle(int cx, int cy, int r, uint16_t fill, uint8_t opa);
	void drawWideLine(float x1, float y1, float x2, float y2, int w, uint16_t c);

	// ── bbox 辅助 ──
	// fruitBBox：含顶部提示文字区域（对非切片/非炸弹水果扩展顶部 ~28px），
	// 使脏区收集与重画集判断天然覆盖提示文字（无需独立跟踪提示区域）。
	static BBox fruitBBox(const FruitNinjaLogic::Fruit& f);
	static BBox particleBBox(const FruitNinjaLogic::Particle& p);
	BBox segBBox(const TrailSegment& s) const;

	// ── 绘制辅助（z 序：背景→水果→粒子→分隔线→刀光→提示文字） ──
	void paintFruit(const FruitNinjaLogic::Fruit& f);
	void paintParticle(const FruitNinjaLogic::Particle& p);
	void paintDivider();
	void paintHints(const FruitNinjaLogic& logic);     // 提示文字（最上层，blit glyph）

	// ── 脏区 / 重画集流程 ──
	void collectDirty(const FruitNinjaLogic& logic);   // 变化对象 → D，更新 prev
	void expandRedraw(const FruitNinjaLogic& logic);   // 与 D 相交筛选 → R（一次遍历，无传播）
	void paint(const FruitNinjaLogic& logic);          // 清 D → 按 z 序重画 R
	void invalidateDirty();                            // 逐区域失效（屏幕坐标）

	// ── 提示文字 glyph 缓存（软件 blit 到 canvas；字符集 ↑↓←→ + A/B/X/Y 共 8 个） ──
	struct GlyphCache
	{
		uint32_t letter = 0;             // Unicode 码点（查找键）
		uint16_t box_w = 0, box_h = 0;   // A8 位图尺寸
		int16_t  ofs_x = 0, ofs_y = 0;   // 绘制偏移（相对 pen/行顶，与 LVGL 公式一致）
		uint16_t adv_w = 0;              // 前进宽
		uint8_t* bitmap = nullptr;       // A8 位图 (box_w*box_h)，PSRAM 私有副本
	};
	GlyphCache m_glyphs[8]{};
	int m_glyphCount = 0;                // 已缓存 glyph 数
	const lv_font_t* m_font{};           // 提示字体（Small）
	int32_t m_lineHeight = 0;            // 提示字体行高
	int32_t m_baseLine = 0;              // 提示字体基线（距行底）
	void preloadGlyphs();                // create() 持锁预取 glyph → 私有缓存
	const GlyphCache* findGlyph(uint32_t letter) const;
	void blitGlyph(int x, int y, const GlyphCache& g, uint16_t color);   // (penX, 行顶)
	void blitText(int x, int y, const char* utf8, uint16_t color);       // y=行顶
	static bool shouldShowHint(const FruitNinjaLogic::Fruit& f);
	static void makeHintText(const FruitNinjaLogic::Fruit& f, char* out, size_t cap);

	// ── 水果图片（esp_driver_jpeg 硬件解码 → PSRAM RGB565，软件圆形 blit；失败回退纯色圆） ──
	struct FruitImage
	{
		const uint16_t* buf;   // RGB565 像素 (w*h)，PSRAM
		int w, h;
		bool valid;
	};
	FruitImage m_fruitImgs[5]{};         // 按 FruitType 索引（值初始化）
	static constexpr FruitImage s_emptyImg{};   // 空图片（valid=false）
	void loadFruitImages();              // create() 持锁解码 5 张 JPG
	void blitImage(int cx, int cy, int r, const FruitImage& img, uint8_t opa);

	// ── HUD / GameOver / 退出按钮（LVGL） ──
	lv_obj_t* m_scoreLabel{};
	lv_obj_t* m_livesLabel{};
	lv_obj_t* m_timerLabel{};
	lv_obj_t* m_hintLabel{};
	lv_obj_t* m_exitBtn{};
	lv_obj_t* m_overlay{};
	lv_obj_t* m_gameOverTitle{};
	lv_obj_t* m_gameOverScore{};
	bool m_gameOverShown = false;

	// 颜色配置（纯色圆，第一版无图片）
	struct FruitColors { uint32_t fill; };
	static constexpr FruitColors COLORS[] = {
		{ 0xff00e676 }, // Watermelon - green
		{ 0xffff5252 }, // Apple - red
		{ 0xffffa726 }, // Orange - orange
		{ 0xffeeff41 }, // Banana - yellow
		{ 0xff555555 }, // Bomb - dark gray
	};
	static uint16_t fruitFillColor(FruitNinjaLogic::FruitType type);
};
