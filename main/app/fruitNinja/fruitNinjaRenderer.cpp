#include "fruitNinjaRenderer.hpp"
#include "display/font.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <cmath>
#include <cstring>
#include <algorithm>

static constexpr char TAG[] = "FruitNinjaRenderer";

constexpr FruitNinjaRenderer::FruitColors FruitNinjaRenderer::COLORS[];

// ============================================================
// 颜色 / 像素辅助
// ============================================================

uint16_t FruitNinjaRenderer::color565(uint32_t rgb)
{
	uint8_t r = (rgb >> 16) & 0xff;
	uint8_t g = (rgb >> 8) & 0xff;
	uint8_t b = rgb & 0xff;
	return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t FruitNinjaRenderer::color565rgb(uint8_t r, uint8_t g, uint8_t b)
{
	return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t FruitNinjaRenderer::blend565(uint16_t dst, uint16_t src, lv_opa_t opa)
{
	if (opa >= LV_OPA_COVER) return src;
	if (opa == LV_OPA_TRANSP) return dst;
	uint8_t dr = (dst >> 11) & 0x1f, dg = (dst >> 5) & 0x3f, db = dst & 0x1f;
	uint8_t sr = (src >> 11) & 0x1f, sg = (src >> 5) & 0x3f, sb = src & 0x1f;
	uint32_t r = (dr * (255 - opa) + sr * opa) / 255;
	uint32_t g = (dg * (255 - opa) + sg * opa) / 255;
	uint32_t b = (db * (255 - opa) + sb * opa) / 255;
	return (uint16_t)((r << 11) | (g << 5) | b);
}

uint16_t FruitNinjaRenderer::fruitFillColor(FruitNinjaLogic::FruitType type)
{
	int idx = static_cast<int>(type);
	if (idx < 0 || idx >= 5) idx = 0;
	return color565(COLORS[idx].fill);
}

// ============================================================
// 创建对象池
// ============================================================

void FruitNinjaRenderer::create(lv_obj_t* parent)
{
	// ── 游戏层 canvas（最底，PSRAM RGB565） ──
	m_canvas = lv_canvas_create(parent);
	lv_obj_remove_style_all(m_canvas);
	lv_obj_set_pos(m_canvas, 0, 0);
	lv_obj_remove_flag(m_canvas, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
	m_buf = static_cast<uint8_t*>(heap_caps_malloc((size_t)W * H * 2,
		MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if (!m_buf)
	{
		ESP_LOGE(TAG, "PSRAM canvas 缓冲分配失败 (%d x %d)", W, H);
		return;
	}
	lv_canvas_set_buffer(m_canvas, m_buf, W, H, LV_COLOR_FORMAT_RGB565);
	lv_draw_buf_t* db = lv_canvas_get_draw_buf(m_canvas);
	if (db) m_stride = db->header.stride;
	else m_stride = (size_t)W * 2;

	m_bg565 = color565(0x0a0a1e);
	fillRect(0, 0, W - 1, H - 1, m_bg565);

	// ── 提示文字 glyph 预缓存（软件 blit，无 LVGL label） ──
	preloadGlyphs();

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

	ESP_LOGI(TAG, "canvas 渲染器创建完成: %dx%d PSRAM RGB565", W, H);
}

// ============================================================
// 析构
// ============================================================

FruitNinjaRenderer::~FruitNinjaRenderer()
{
	if (m_buf)
	{
		heap_caps_free(m_buf);
		m_buf = nullptr;
	}
	for (auto& g : m_glyphs)
		if (g.bitmap)
		{
			heap_caps_free(g.bitmap);
			g.bitmap = nullptr;
		}
}

// ============================================================
// bbox 辅助
// ============================================================

FruitNinjaRenderer::BBox FruitNinjaRenderer::fruitBBox(const FruitNinjaLogic::Fruit& f)
{
	BBox b;
	b.active = true;
	float r = FruitNinjaLogic::getFruitRadius(f.type);
	if (f.sliced)
	{
		// 缩小一半圆（左上角对齐）
		int diam = static_cast<int>(r);
		int ox = static_cast<int>(f.x) - static_cast<int>(r);
		int oy = static_cast<int>(f.y) - static_cast<int>(r);
		b.x1 = ox; b.y1 = oy;
		b.x2 = ox + diam - 1;
		b.y2 = oy + diam - 1;
	}
	else
	{
		b.x1 = static_cast<int>(f.x - r);
		b.y1 = static_cast<int>(f.y - r);
		b.x2 = static_cast<int>(f.x + r);
		b.y2 = static_cast<int>(f.y + r);

		// 顶部提示文字区域并入 bbox（非炸弹水果；blitText 起点 (x-16, y-r-24)，行高 ≤Small 字体）
		if (f.type != FruitNinjaLogic::FruitType::Bomb)
		{
			constexpr int HINT_W = 56;
			constexpr int HINT_H = 36;
			int hx1 = static_cast<int>(f.x) - 16;
			int hy1 = static_cast<int>(f.y - r) - HINT_H;
			int hx2 = hx1 + HINT_W - 1;
			int hy2 = static_cast<int>(f.y - r) - 1;
			if (hx1 < b.x1) b.x1 = hx1;
			if (hy1 < b.y1) b.y1 = hy1;
			if (hx2 > b.x2) b.x2 = hx2;
			if (hy2 > b.y2) b.y2 = hy2;
		}
	}
	return b;
}

FruitNinjaRenderer::BBox FruitNinjaRenderer::particleBBox(const FruitNinjaLogic::Particle& p)
{
	int r = p.size / 2;
	if (r < 1) r = 1;
	BBox b;
	b.active = true;
	b.x1 = static_cast<int>(p.x) - r;
	b.y1 = static_cast<int>(p.y) - r;
	b.x2 = static_cast<int>(p.x) + r;
	b.y2 = static_cast<int>(p.y) + r;
	return b;
}

FruitNinjaRenderer::BBox FruitNinjaRenderer::segBBox(const TrailSegment& s) const
{
	constexpr int R = 7;   // 刀光半宽（14px）
	BBox b;
	b.active = true;
	b.x1 = static_cast<int>(std::min(s.x1, s.x2)) - R;
	b.y1 = static_cast<int>(std::min(s.y1, s.y2)) - R;
	b.x2 = static_cast<int>(std::max(s.x1, s.x2)) + R;
	b.y2 = static_cast<int>(std::max(s.y1, s.y2)) + R;
	return b;
}

// ============================================================
// 脏区集合
// ============================================================

void FruitNinjaRenderer::dirtyAdd(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
	if (x2 < 0 || y2 < 0 || x1 >= W || y1 >= H) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= W) x2 = W - 1;
	if (y2 >= H) y2 = H - 1;
	if (x1 > x2 || y1 > y2) return;

	// 与已有区域重叠 → 扩展（不合并不相交区域，避免虚胖）
	for (int i = 0; i < m_dirtyCount; i++)
	{
		auto& d = m_dirty[i];
		if (d.x1 <= x2 && d.x2 >= x1 && d.y1 <= y2 && d.y2 >= y1)
		{
			d.x1 = std::min(d.x1, x1);
			d.y1 = std::min(d.y1, y1);
			d.x2 = std::max(d.x2, x2);
			d.y2 = std::max(d.y2, y2);
			return;
		}
	}
	if (m_dirtyCount < 64)
	{
		auto& d = m_dirty[m_dirtyCount];
		d.x1 = x1; d.y1 = y1; d.x2 = x2; d.y2 = y2;
		m_dirtyCount++;
	}
	else
	{
		// 满：并入最后一个（兜底防溢出）
		auto& d = m_dirty[63];
		d.x1 = std::min(d.x1, x1);
		d.y1 = std::min(d.y1, y1);
		d.x2 = std::max(d.x2, x2);
		d.y2 = std::max(d.y2, y2);
	}
}

bool FruitNinjaRenderer::dirtyIntersects(const BBox& b) const
{
	if (!b.active) return false;
	for (int i = 0; i < m_dirtyCount; i++)
	{
		const auto& d = m_dirty[i];
		if (d.x1 <= b.x2 && d.x2 >= b.x1 && d.y1 <= b.y2 && d.y2 >= b.y1)
			return true;
	}
	return false;
}

// ============================================================
// 软件绘制（直接写 canvas buffer）
// ============================================================

void FruitNinjaRenderer::fillRect(int x1, int y1, int x2, int y2, uint16_t c)
{
	if (!m_buf) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= W) x2 = W - 1;
	if (y2 >= H) y2 = H - 1;
	if (x1 > x2 || y1 > y2) return;

	// 填首行 → 后续行 memcpy（行内容相同，内存带宽级，避免逐像素写入）
	int w = x2 - x1 + 1;
	uint16_t* row0 = reinterpret_cast<uint16_t*>(m_buf + (size_t)y1 * m_stride) + x1;
	for (int x = 0; x < w; x++) row0[x] = c;
	const size_t strideWords = m_stride / 2;
	for (int y = y1 + 1; y <= y2; y++)
		memcpy(row0 + (size_t)(y - y1) * strideWords, row0, (size_t)w * 2);
}

void FruitNinjaRenderer::fillCircle(int cx, int cy, int r, uint16_t fill, uint8_t opa)
{
	if (!m_buf || r <= 0) return;
	int x1 = cx - r, y1 = cy - r, x2 = cx + r, y2 = cy + r;
	if (x2 < 0 || y2 < 0 || x1 >= W || y1 >= H) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= W) x2 = W - 1;
	if (y2 >= H) y2 = H - 1;

	// 扫描线：每行求半宽直接填区间，免逐像素乘方判断；顶部高光按行 blend（dy 固定）
	const int r2 = r * r;
	for (int y = y1; y <= y2; y++)
	{
		int dy = y - cy;
		int dy2 = dy * dy;
		if (dy2 > r2) continue;
		int half = (int)sqrtf((float)(r2 - dy2));
		int xs = cx - half, xe = cx + half;
		if (xs < x1) xs = x1;
		if (xe > x2) xe = x2;
		if (xs > xe) continue;
		uint16_t* row = reinterpret_cast<uint16_t*>(m_buf + (size_t)y * m_stride);
		if (opa >= LV_OPA_COVER)
		{
			if (dy < 0)
			{
				uint8_t hi = static_cast<uint8_t>(((-dy) * 96) / r);
				uint16_t c = blend565(fill, 0xffff, hi);
				for (int x = xs; x <= xe; x++) row[x] = c;
			}
			else
			{
				for (int x = xs; x <= xe; x++) row[x] = fill;
			}
		}
		else
		{
			for (int x = xs; x <= xe; x++) row[x] = blend565(row[x], fill, opa);
		}
	}
}

void FruitNinjaRenderer::drawWideLine(float x1, float y1, float x2, float y2, int w, uint16_t c)
{
	if (!m_buf) return;
	const float half = w * 0.5f;
	float dxs = x2 - x1, dys = y2 - y1;
	float len2 = dxs * dxs + dys * dys;
	if (len2 < 0.0001f)
	{
		fillCircle(static_cast<int>(x1), static_cast<int>(y1), static_cast<int>(half), c, LV_OPA_COVER);
		return;
	}
	int ix1 = static_cast<int>(std::min(x1, x2)) - (int)half - 1;
	int iy1 = static_cast<int>(std::min(y1, y2)) - (int)half - 1;
	int ix2 = static_cast<int>(std::max(x1, x2)) + (int)half + 1;
	int iy2 = static_cast<int>(std::max(y1, y2)) + (int)half + 1;
	if (ix2 < 0 || iy2 < 0 || ix1 >= W || iy1 >= H) return;
	if (ix1 < 0) ix1 = 0;
	if (iy1 < 0) iy1 = 0;
	if (ix2 >= W) ix2 = W - 1;
	if (iy2 >= H) iy2 = H - 1;
	const float half2 = half * half;
	for (int y = iy1; y <= iy2; y++)
	{
		uint16_t* row = reinterpret_cast<uint16_t*>(m_buf + (size_t)y * m_stride);
		for (int x = ix1; x <= ix2; x++)
		{
			float t = ((x - x1) * dxs + (y - y1) * dys) / len2;
			if (t < 0.0f) t = 0.0f;
			else if (t > 1.0f) t = 1.0f;
			float px = x1 + t * dxs;
			float py = y1 + t * dys;
			float dx = x - px, dy = y - py;
			if (dx * dx + dy * dy <= half2)
				row[x] = c;
		}
	}
}

// ============================================================
// 绘制辅助（z 序：背景→水果→粒子→分隔线→刀光）
// ============================================================

void FruitNinjaRenderer::paintFruit(const FruitNinjaLogic::Fruit& f)
{
	float r = FruitNinjaLogic::getFruitRadius(f.type);
	uint16_t fill = fruitFillColor(f.type);
	if (f.sliced)
	{
		// 缩小一半圆 + 淡出（左上角对齐，与旧视觉一致）
		int diam = static_cast<int>(r);
		int cx = static_cast<int>(f.x) - static_cast<int>(r) + diam / 2;
		int cy = static_cast<int>(f.y) - static_cast<int>(r) + diam / 2;
		fillCircle(cx, cy, diam / 2, fill, f.opacity);
	}
	else
	{
		fillCircle(static_cast<int>(f.x), static_cast<int>(f.y), static_cast<int>(r), fill, LV_OPA_COVER);
	}
}

void FruitNinjaRenderer::paintParticle(const FruitNinjaLogic::Particle& p)
{
	int r = p.size / 2;
	if (r < 1) r = 1;
	fillCircle(static_cast<int>(p.x), static_cast<int>(p.y), r,
		color565rgb(p.r, p.g, p.b), p.opacity);
}

void FruitNinjaRenderer::paintDivider()
{
	// 2P 分隔线：半透明白竖线（2px），blend 到现有内容
	int x = W / 2 - 1;
	const lv_opa_t opa = 68;   // 0x44 alpha ≈ 26%
	for (int y = 0; y < H; y++)
	{
		uint16_t* p = reinterpret_cast<uint16_t*>(m_buf + (size_t)y * m_stride + (size_t)x * 2);
		p[0] = blend565(p[0], 0xffff, opa);
		p[1] = blend565(p[1], 0xffff, opa);
	}
}

// ============================================================
// 脏区收集 / 相交扩散 / 重绘
// ============================================================

void FruitNinjaRenderer::collectDirty(const FruitNinjaLogic& logic)
{
	dirtyClear();

	// ── 水果 ──
	int fruitCount = logic.getFruitCount();
	const auto* fruits = logic.getFruits();
	for (int i = 0; i < MAX_FRUITS; i++)
	{
		const auto& f = fruits[i];
		BBox cur = (i < fruitCount && f.active) ? fruitBBox(f) : BBox{};
		BBox& prev = m_prevFruit[i];
		bool changed = (cur.active != prev.active)
			|| (cur.active && (cur.x1 != prev.x1 || cur.y1 != prev.y1 ||
				cur.x2 != prev.x2 || cur.y2 != prev.y2));
		if (cur.active && f.sliced && !changed) changed = true;   // 切片淡出须重画
		if (changed)
		{
			if (prev.active) dirtyAdd(prev.x1, prev.y1, prev.x2, prev.y2);
			if (cur.active) dirtyAdd(cur.x1, cur.y1, cur.x2, cur.y2);
		}
		prev = cur;
	}

	// ── 粒子 ──
	int particleCount = logic.getParticleCount();
	const auto* particles = logic.getParticles();
	for (int i = 0; i < MAX_PARTICLES; i++)
	{
		const auto& p = particles[i];
		BBox cur = (i < particleCount && p.active) ? particleBBox(p) : BBox{};
		BBox& prev = m_prevParticle[i];
		bool changed = (cur.active != prev.active)
			|| (cur.active && (cur.x1 != prev.x1 || cur.y1 != prev.y1 ||
				cur.x2 != prev.x2 || cur.y2 != prev.y2));
		if (changed)
		{
			if (prev.active) dirtyAdd(prev.x1, prev.y1, prev.x2, prev.y2);
			if (cur.active) dirtyAdd(cur.x1, cur.y1, cur.x2, cur.y2);
		}
		prev = cur;
	}

	// ── 刀光段（超时消失：仅新增/移除/回绕顶掉标脏，不每帧重绘） ──
	for (int i = 0; i < kMaxSegments; i++)
	{
		BBox cur = m_segments[i].active ? segBBox(m_segments[i]) : BBox{};
		BBox& prev = m_prevSeg[i];
		// 比较 bbox 而非仅 active：回绕顶掉旧段时 active 不变（true→true）也须标脏
		bool changed = (cur.active != prev.active)
			|| (cur.active && (cur.x1 != prev.x1 || cur.y1 != prev.y1 ||
				cur.x2 != prev.x2 || cur.y2 != prev.y2));
		if (changed)
		{
			if (prev.active) dirtyAdd(prev.x1, prev.y1, prev.x2, prev.y2);
			if (cur.active) dirtyAdd(cur.x1, cur.y1, cur.x2, cur.y2);
		}
		prev = cur;
	}
}

void FruitNinjaRenderer::expandRedraw(const FruitNinjaLogic& logic)
{
	memset(m_redrawFruit, 0, sizeof(m_redrawFruit));
	memset(m_redrawParticle, 0, sizeof(m_redrawParticle));
	memset(m_redrawSeg, 0, sizeof(m_redrawSeg));
	m_redrawDivider = false;

	// 重画集 = 与脏区集合 D 相交的 active 对象（一次遍历，无 O(n²) 传播）。
	// 正确性：清 D 时被清除影响的对象，其 bbox 必然与 D 相交 → 进 R 重画；
	// paint 按 z 序从底到顶绘制，上层对象重画自然覆盖下层，被覆盖的下层无需单独重画。
	const auto* fruits = logic.getFruits();
	const auto* particles = logic.getParticles();
	for (int i = 0; i < MAX_FRUITS; i++)
		if (fruits[i].active && dirtyIntersects(fruitBBox(fruits[i])))
			m_redrawFruit[i] = true;
	for (int i = 0; i < MAX_PARTICLES; i++)
		if (particles[i].active && dirtyIntersects(particleBBox(particles[i])))
			m_redrawParticle[i] = true;
	for (int i = 0; i < kMaxSegments; i++)
		if (m_segments[i].active && dirtyIntersects(segBBox(m_segments[i])))
			m_redrawSeg[i] = true;
	if (logic.getPlayerCount() >= 2)
	{
		BBox db;
		db.active = true;
		db.x1 = W / 2 - 1; db.y1 = 0;
		db.x2 = W / 2;     db.y2 = H - 1;
		if (dirtyIntersects(db)) m_redrawDivider = true;
	}
}

void FruitNinjaRenderer::paint(const FruitNinjaLogic& logic)
{
	// 1. 清 D 各区域背景
	for (int i = 0; i < m_dirtyCount; i++)
	{
		const auto& d = m_dirty[i];
		fillRect(d.x1, d.y1, d.x2, d.y2, m_bg565);
	}

	// 2. 按 z 序重画 R：水果 → 粒子 → 分隔线 → 刀光 → 提示文字
	for (int i = 0; i < MAX_FRUITS; i++)
		if (m_redrawFruit[i])
			paintFruit(logic.getFruits()[i]);
	for (int i = 0; i < MAX_PARTICLES; i++)
		if (m_redrawParticle[i])
			paintParticle(logic.getParticles()[i]);
	if (m_redrawDivider)
		paintDivider();
	for (int i = 0; i < kMaxSegments; i++)
		if (m_redrawSeg[i])
			drawWideLine(m_segments[i].x1, m_segments[i].y1,
				m_segments[i].x2, m_segments[i].y2, 14, 0xffff);
	paintHints(logic);
}

void FruitNinjaRenderer::invalidateDirty()
{
	if (!m_canvas || m_dirtyCount == 0) return;
	if (!m_initialInvalidated)
	{
		// 首帧强制整 canvas 失效（此时布局已完成，coords 有效）
		lv_obj_invalidate(m_canvas);
		m_initialInvalidated = true;
		return;
	}
	lv_area_t c;
	lv_obj_get_coords(m_canvas, &c);
	for (int i = 0; i < m_dirtyCount; i++)
	{
		const auto& d = m_dirty[i];
		lv_area_t scr;
		lv_area_set(&scr, c.x1 + d.x1, c.y1 + d.y1, c.x1 + d.x2, c.y1 + d.y2);
		lv_obj_invalidate_area(m_canvas, &scr);
	}
}

// ============================================================
// 提示文字 — glyph 预缓存 + 软件 blit（替代 LVGL label，游戏层零 LVGL 对象）
// ============================================================

bool FruitNinjaRenderer::shouldShowHint(const FruitNinjaLogic::Fruit& f)
{
	return f.active && !f.sliced && f.type != FruitNinjaLogic::FruitType::Bomb;
}

void FruitNinjaRenderer::makeHintText(const FruitNinjaLogic::Fruit& f, char* out, size_t cap)
{
	snprintf(out, cap, "%s%s",
		FruitNinjaLogic::dirToArrow(f.requiredDir),
		FruitNinjaLogic::btnToChar(f.requiredBtn));
}

void FruitNinjaRenderer::preloadGlyphs()
{
	m_font = FontLoader::getDefault(FontLoader::FontSize::Small);
	if (!m_font) return;
	m_lineHeight = lv_font_get_line_height(m_font);
	m_baseLine = m_font->base_line;

	// 提示字符集：↑ ↓ ← → + A/B/X/Y（UTF-8 箭头 = U+2191/2193/2190/2192）
	static const uint32_t letters[8] = { 0x2191, 0x2193, 0x2190, 0x2192, 'A', 'B', 'X', 'Y' };

	// 临时缓冲承接 glyph 位图（A8；Small 字体最大字形 ≤64px）
	lv_draw_buf_t* tmp = lv_draw_buf_create(64, 64, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
	if (!tmp) return;

	for (uint32_t ch : letters)
	{
		if (m_glyphCount >= 8) break;
		lv_font_glyph_dsc_t dsc;
		if (!lv_font_get_glyph_dsc(m_font, &dsc, ch, 0)) continue;
		const void* gbmp = lv_font_get_glyph_bitmap(&dsc, tmp);
		if (!gbmp || dsc.box_w == 0 || dsc.box_h == 0 ||
			dsc.format != LV_FONT_GLYPH_FORMAT_A8)
		{
			lv_font_glyph_release_draw_data(&dsc);
			continue;
		}
		// freetype（IMAGE 模式）返回 lv_draw_buf_t*：A8 位图按 header.stride 对齐存储，
		// 不能当紧凑数组直接读；需按 stride 逐行复制成紧凑 box_w×box_h 私有副本。
		const lv_draw_buf_t* gbuf = static_cast<const lv_draw_buf_t*>(gbmp);
		const uint8_t* bmp = static_cast<const uint8_t*>(gbuf->data);
		uint32_t stride = gbuf->header.stride;

		GlyphCache& g = m_glyphs[m_glyphCount];
		g.letter = ch;
		g.box_w = dsc.box_w;
		g.box_h = dsc.box_h;
		g.ofs_x = dsc.ofs_x;
		g.ofs_y = dsc.ofs_y;
		g.adv_w = dsc.adv_w;
		g.bitmap = static_cast<uint8_t*>(heap_caps_malloc((size_t)g.box_w * g.box_h,
			MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
		if (g.bitmap)
			for (int j = 0; j < g.box_h; j++)
				memcpy(g.bitmap + (size_t)j * g.box_w,
					bmp + (size_t)j * stride, g.box_w);
		lv_font_glyph_release_draw_data(&dsc);
		m_glyphCount++;
	}

	lv_draw_buf_destroy(tmp);
}

const FruitNinjaRenderer::GlyphCache* FruitNinjaRenderer::findGlyph(uint32_t letter) const
{
	for (int i = 0; i < m_glyphCount; i++)
		if (m_glyphs[i].letter == letter)
			return &m_glyphs[i];
	return nullptr;
}

void FruitNinjaRenderer::blitGlyph(int x, int y, const GlyphCache& g, uint16_t color)
{
	if (!m_buf || !g.bitmap) return;
	// 与 LVGL 一致（lv_draw_label.c:624）：glyph 顶 = 行顶 + (line_height - base_line) - box_h - ofs_y
	int gx = x + g.ofs_x;
	int gy = y + (m_lineHeight - m_baseLine) - g.box_h - g.ofs_y;
	for (int j = 0; j < g.box_h; j++)
	{
		int py = gy + j;
		if (py < 0 || py >= H) continue;
		uint16_t* row = reinterpret_cast<uint16_t*>(m_buf + (size_t)py * m_stride);
		const uint8_t* src = g.bitmap + (size_t)j * g.box_w;
		for (int i = 0; i < g.box_w; i++)
		{
			int px = gx + i;
			if (px < 0 || px >= W) continue;
			uint8_t a = src[i];
			if (a >= 255) row[px] = color;
			else if (a > 0) row[px] = blend565(row[px], color, a);
		}
	}
}

void FruitNinjaRenderer::blitText(int x, int y, const char* utf8, uint16_t color)
{
	if (!m_buf) return;
	int penX = x;
	while (utf8 && *utf8)
	{
		uint32_t ch;
		uint8_t c0 = static_cast<uint8_t>(*utf8);
		if (c0 < 0x80)
		{
			ch = c0;
			utf8 += 1;
		}
		else if ((c0 & 0xE0) == 0xC0)
		{
			ch = ((c0 & 0x1F) << 6) | (static_cast<uint8_t>(utf8[1]) & 0x3F);
			utf8 += 2;
		}
		else if ((c0 & 0xF0) == 0xE0)
		{
			ch = ((c0 & 0x0F) << 12) | ((static_cast<uint8_t>(utf8[1]) & 0x3F) << 6)
				| (static_cast<uint8_t>(utf8[2]) & 0x3F);
			utf8 += 3;
		}
		else
		{
			utf8 += 1;
			continue;
		}

		const GlyphCache* g = findGlyph(ch);
		if (!g)
		{
			penX += 12;
			continue;
		}
		blitGlyph(penX, y, *g, color);
		penX += g->adv_w;
	}
}

void FruitNinjaRenderer::paintHints(const FruitNinjaLogic& logic)
{
	const auto* fruits = logic.getFruits();
	for (int i = 0; i < MAX_FRUITS; i++)
	{
		const auto& f = fruits[i];
		if (!shouldShowHint(f) || !m_redrawFruit[i]) continue;
		float r = FruitNinjaLogic::getFruitRadius(f.type);
		char buf[8];
		makeHintText(f, buf, sizeof(buf));
		blitText(static_cast<int>(f.x - 16), static_cast<int>(f.y - r - 24), buf, 0xffff);
	}
}

// ============================================================
// 每帧渲染
// ============================================================

void FruitNinjaRenderer::render(const FruitNinjaLogic& logic)
{
	auto state = logic.getState();
	bool isPlaying = (state == FruitNinjaLogic::State::Playing);

	// ── 游戏层（canvas 脏区重绘） ──
	if (isPlaying)
	{
		collectDirty(logic);
		expandRedraw(logic);
		paint(logic);
		invalidateDirty();
	}

	// ── 退出按钮（游戏中显示，GameOver 时隐藏） ──
	if (isPlaying)
		lv_obj_clear_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);
	else
		lv_obj_add_flag(m_exitBtn, LV_OBJ_FLAG_HIDDEN);

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

// ============================================================
// 刀光线段 — 追加一条线段段（触摸每帧产生；段池回绕，超时消失）
// ============================================================

void FruitNinjaRenderer::addTrailSegment(float x1, float y1, float x2, float y2)
{
	auto& s = m_segments[m_segWrite];
	s.x1 = x1; s.y1 = y1;
	s.x2 = x2; s.y2 = y2;
	s.life = TRAIL_LIFETIME;   // 回绕覆盖旧段时重置生命周期
	s.active = true;

	m_segWrite = (m_segWrite + 1) % kMaxSegments;
}

// ============================================================
// 白刃轨迹 — 老化（递减各段剩余生命，超时后消失）
// ============================================================

void FruitNinjaRenderer::ageTrail(float dt)
{
	// 超时消失：每段剩余生命递减，归零后消失（无半透明渐隐，性能最优）
	for (int i = 0; i < kMaxSegments; i++)
	{
		auto& s = m_segments[i];
		if (!s.active) continue;

		s.life -= dt;
		if (s.life <= 0.0f)
			s.active = false;   // 超时 → 下一帧 render 清除 canvas 残留
	}
}

// ============================================================
// 白刃轨迹 — 清空（保留上帧标记，下一帧 render 清除 canvas 残留）
// ============================================================

void FruitNinjaRenderer::clearTrail()
{
	for (int i = 0; i < kMaxSegments; i++)
		m_segments[i].active = false;
	m_segWrite = 0;
}
