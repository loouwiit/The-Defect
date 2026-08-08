#pragma once

#include "lvgl.h"
#include "fruitNinjaLogic.hpp"
#include <functional>

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

	/** @brief 追加一条刀光线段段（触摸移动产生；player 为段归属玩家，2P 分屏用） */
	void addTrailSegment(float x1, float y1, float x2, float y2, int player);

	/** @brief 每帧统一老化（递减各段剩余生命，超时后消失） */
	void ageTrail(float dt);

	/** @brief 一键清空所有线段段（保留上帧标记，下一帧 render 清除 canvas 残留） */
	void clearTrail();

	/** @brief 遍历当前所有活跃刀光段（残留 0.5s 内持续触发切割；gameLoop 线程调用，无锁） */
	void forEachTrailSegment(const std::function<void(float, float, float, float, int)>& cb) const;

	/** @brief 设置某手指的头部人物（触摸时跟随手指，沿滑动方向旋转） */
	void setTouchHead(int index, float x, float y, float dirX, float dirY, bool active);

	/** @brief 清空全部头部人物 */
	void clearTouchHeads();

private:
	static constexpr int W = 1280;
	static constexpr int H = 720;
	static constexpr int MaxFruits = FruitNinjaLogic::MaxFruits;
	static constexpr int MaxParticles = FruitNinjaLogic::MaxParticles;

	// 刀光段池 + 超时不透明：段保持 TRAIL_LIFETIME 秒后瞬间消失（无逐帧淡出，性能最优）
	// 64 段：60fps 持续划线 0.5s ≈ 30 段，池够大不顶掉；触摸停止后从尾部逐段消失
	static constexpr int kMaxSegments = 64;
	static constexpr float kMaxSegLen = 40.0f;   // 单段最大长度（px）：更长的线段在 addTrailSegment 拆分为多段
	static constexpr float TRAIL_LIFETIME = 0.3f;   // 段超时（秒）

	static constexpr int kMaxLeaves = 48;        // 竹叶粒子池（切割时随机向四周飘散）
	static constexpr int kMaxActiveLeaves = 6;   // 同时活跃竹叶上限（超限跳过生成，防卡顿）
	static constexpr int kMaxTouchHeads = 5;     // 触摸头部人物上限（对齐 GT911 槽位）
	static constexpr float kQiuyuanHeight = 100.0f;   // qiuyuan 人物显示高度（px，按图高等比缩放）
	static constexpr float kQiuyuanTrail = 110.0f;    // 人物跟随手指的"身后"偏移距离（px），避免被手遮住
	static constexpr uint32_t kBambooGreen = 0x9acd32;   // 竹绿色（刀光拖尾，sword 图加载失败时回退）

	// ── sword 光效刀光（sword.jpg：深色底光效图 → 亮度抠 alpha + 加法混合，逐段旋转 + 残影渐隐） ──
	static constexpr float kSwordScale = 2.0f;        // 每段光效缩放倍率（相对段长/图长边；调残影大小/密度）
	static constexpr float kSwordAngleOffset = 0.0f;  // 图片主轴朝向额外偏移（rad），按 sword.jpg 实际朝向微调
	static constexpr float kSwordAlphaGain = 1.5f;    // 亮度→alpha 增益（深底抠图阈值，越高背景抠得越狠）
	static constexpr uint8_t kSwordAlphaFloor = 32;   // 亮度下限：低于此视为深底背景 → 透明

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
		uint8_t player = 0;    // 段归属玩家（0=P1, 1=P2），残留切割按玩家过滤
		bool active = false;
	};
	TrailSegment m_segments[kMaxSegments]{};
	int m_segWrite = 0;
	BBox m_prevSeg[kMaxSegments];

	// ── 竹叶粒子（切割时随机向四周飘散；旋转缩放 + 重力 + 渐隐，纯渲染层） ──
	struct LeafParticle
	{
		float x = 0, y = 0;              // 位置（px）
		float vx = 0, vy = 0;            // 速度（px/s）
		float angle = 0, angVel = 0;     // 旋转角（rad）/ 角速度（rad/s）
		float scale = 1.0f;              // 缩放（相对原图）
		float life = 0;                  // 剩余生命（秒），ageLeaves 递减，<=0 消失
		float maxLife = 0;               // 出生时寿命（渐隐比例基准，防除以零）
		bool active = false;
	};
	LeafParticle m_leaves[kMaxLeaves]{};
	int m_leafWrite = 0;
	int m_activeLeafCount = 0;   // 当前活跃竹叶数（上限 kMaxActiveLeaves）
	BBox m_prevLeaf[kMaxLeaves];

	// ── 触摸头部人物（qiuyuan.jpg：跟随手指 + 沿滑动方向旋转） ──
	struct TouchHead
	{
		float x = 0, y = 0;            // 位置（px）
		float dirX = 0, dirY = -1;     // 滑动方向（旋转朝向，默认朝上）
		bool active = false;
	};
	TouchHead m_touchHeads[kMaxTouchHeads]{};
	BBox m_prevHead[kMaxTouchHeads];

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
	bool m_redrawLeaf[kMaxLeaves]{};
	bool m_redrawHead[kMaxTouchHeads]{};
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
	BBox leafBBox(const LeafParticle& l) const;   // 半对角外接矩形（旋转安全超集）
	BBox headBBox(const TouchHead& h) const;      // qiuyuan 人物半对角外接矩形（旋转安全超集）

	// ── 绘制辅助（z 序：背景→水果→粒子→分隔线→刀光→竹叶→提示文字） ──
	void paintFruit(const FruitNinjaLogic::Fruit& f);
	void paintParticle(const FruitNinjaLogic::Particle& p);
	void paintDivider();
	void paintLeaves();                              // 竹叶（刀光之上，提示文字之下）
	void paintTouchHeads();                          // qiuyuan 人物（竹叶之上，提示文字之下）
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

	// ── 全屏背景图（esp_driver_jpeg 硬件解码 → 等比 cover 缩放居中裁剪到 W×H，PSRAM RGB565） ──
	FruitImage m_bg{};                            // W×H 背景缓冲；valid=false 时回退纯色 m_bg565
	void loadBackground();                        // create() 持锁解码 + cover 缩放
	void paintBgRect(int x1, int y1, int x2, int y2);   // 从背景图复制矩形区域到 canvas（清脏区用）

	// ── 竹叶精灵图（bamboo leaves.jpg：硬件解码 → 白色背景抠图 alpha → PSRAM；失败回退不生成） ──
	FruitImage m_leafImg{};               // RGB565 像素（最大边 ≤128）
	uint8_t* m_leafAlpha = nullptr;       // 逐像素 alpha（白色背景抠除，0-255）
	void loadLeafImage();                 // create() 持锁解码 + 抠图
	void blitLeaf(float cx, float cy, const LeafParticle& l);  // 渐隐 + 旋转精灵绘制
	void spawnLeaf(float x, float y, float segDx, float segDy); // 在切割点附近生成一片竹叶（向四周散开）
	void ageLeaves(float dt);             // 竹叶物理：重力 + 位置/角度积分 + 生命递减

	// ── qiuyuan 人物精灵（触摸头部：跟随手指 + 沿滑动方向旋转） ──
	FruitImage m_qiuyuanImg{};            // RGB565 像素
	uint8_t* m_qiuyuanAlpha = nullptr;    // 逐像素 alpha
	void loadQiuyuanImage();              // create() 持锁解码 + 抠图
	void blitQiuyuan(float cx, float cy, float angle);   // 固定显示高度旋转绘制
	void blitRotatedSprite(const FruitImage& img, const uint8_t* alpha,
		float cx, float cy, float scale, float angle, uint8_t fadeByte);   // 通用旋转精灵绘制（竹叶/人物共用）

	// ── sword 光效刀光（sword.jpg：深色底光效 → 亮度抠 alpha；沿轨迹逐段旋转 + 残影渐隐） ──
	FruitImage m_swordImg{};              // RGB565 像素（最大边 ≤ 256）
	uint8_t* m_swordAlpha = nullptr;      // 逐像素 alpha（亮度抠图，0-255）
	void loadSwordImage();                // create() 持锁解码 + 亮度抠图
	void blitSwordSegment(float x1, float y1, float x2, float y2, float life);  // 单段旋转绘制 + 残影渐隐
	void blitRotatedSpriteAdd(const FruitImage& img, const uint8_t* alpha,
		float cx, float cy, float scale, float angle, uint8_t fadeByte);   // 加法混合旋转精灵（光效用）

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
