#include "fruitNinjaRenderer.hpp"
#include "display/font.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "driver/jpeg_decode.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

static constexpr char TAG[] = "FruitNinjaRenderer";

constexpr FruitNinjaRenderer::FruitColors FruitNinjaRenderer::COLORS[];

// 随机浮点（[a, b)）
static float randFloat(float a, float b)
{
	return a + (b - a) * ((float)rand() / (float)RAND_MAX);
}

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
	loadBackground();                        // 解码全屏背景图（失败回退纯色）
	if (m_bg.valid)
		paintBgRect(0, 0, W - 1, H - 1);
	else
		fillRect(0, 0, W - 1, H - 1, m_bg565);

	// ── 提示文字 glyph 预缓存（软件 blit，无 LVGL label） ──
	preloadGlyphs();

	// ── 水果图片硬件解码（失败自动回退纯色圆） ──
	loadFruitImages();

	// ── 竹叶精灵图硬件解码 + 白色抠图（失败回退不生成竹叶） ──
	loadLeafImage();

	// ── qiuyuan 人物精灵图硬件解码 + 白色抠图（失败回退不显示人物） ──
	loadQiuyuanImage();

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
	for (auto& im : m_fruitImgs)
		if (im.buf)
		{
			heap_caps_free(const_cast<uint16_t*>(im.buf));
			im.buf = nullptr;
			im.valid = false;
		}
	if (m_bg.buf)
	{
		heap_caps_free(const_cast<uint16_t*>(m_bg.buf));
		m_bg.buf = nullptr;
		m_bg.valid = false;
	}
	if (m_leafImg.buf)
	{
		heap_caps_free(const_cast<uint16_t*>(m_leafImg.buf));
		m_leafImg.buf = nullptr;
		m_leafImg.valid = false;
	}
	if (m_leafAlpha)
	{
		heap_caps_free(m_leafAlpha);
		m_leafAlpha = nullptr;
	}
	if (m_qiuyuanImg.buf)
	{
		heap_caps_free(const_cast<uint16_t*>(m_qiuyuanImg.buf));
		m_qiuyuanImg.buf = nullptr;
		m_qiuyuanImg.valid = false;
	}
	if (m_qiuyuanAlpha)
	{
		heap_caps_free(m_qiuyuanAlpha);
		m_qiuyuanAlpha = nullptr;
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

		// 顶部提示文字区域并入 bbox（未切片水果含炸弹；blitText 起点 (x-16, y-r-24)，行高 ≤Small 字体）
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

FruitNinjaRenderer::BBox FruitNinjaRenderer::leafBBox(const LeafParticle& l) const
{
	// 半对角外接矩形（旋转安全超集）：l.scale * 图半对角
	if (!m_leafImg.valid) return {};
	float hr = 0.5f * l.scale * hypotf((float)m_leafImg.w, (float)m_leafImg.h);
	int R = static_cast<int>(ceilf(hr)) + 1;
	BBox b;
	b.active = true;
	b.x1 = static_cast<int>(l.x) - R;
	b.y1 = static_cast<int>(l.y) - R;
	b.x2 = static_cast<int>(l.x) + R;
	b.y2 = static_cast<int>(l.y) + R;
	return b;
}

FruitNinjaRenderer::BBox FruitNinjaRenderer::headBBox(const TouchHead& h) const
{
	// qiuyuan 人物：固定显示高度 → 半对角外接矩形（旋转安全超集）
	if (!m_qiuyuanImg.valid) return {};
	float scale = kQiuyuanHeight / (float)m_qiuyuanImg.h;
	float hr = 0.5f * scale * hypotf((float)m_qiuyuanImg.w, (float)m_qiuyuanImg.h);
	int R = static_cast<int>(ceilf(hr)) + 1;
	BBox b;
	b.active = true;
	b.x1 = static_cast<int>(h.x) - R;
	b.y1 = static_cast<int>(h.y) - R;
	b.x2 = static_cast<int>(h.x) + R;
	b.y2 = static_cast<int>(h.y) + R;
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

void FruitNinjaRenderer::blitImage(int cx, int cy, int r, const FruitImage& img, uint8_t opa)
{
	if (!m_buf || !img.valid || r <= 0) return;
	int x1 = cx - r, y1 = cy - r, x2 = cx + r, y2 = cy + r;
	if (x2 < 0 || y2 < 0 || x1 >= W || y1 >= H) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= W) x2 = W - 1;
	if (y2 >= H) y2 = H - 1;

	// 圆形裁剪 + 最近邻缩放 blit（扫描线：每行固定 sy，圆内区间填）
	const int r2 = r * r;
	const int d = 2 * r;
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
		int sy = (y - cy + r) * img.h / d;
		if (sy < 0) sy = 0;
		if (sy >= img.h) sy = img.h - 1;
		uint16_t* row = reinterpret_cast<uint16_t*>(m_buf + (size_t)y * m_stride);
		const uint16_t* srcRow = img.buf + (size_t)sy * img.w;
		for (int x = xs; x <= xe; x++)
		{
			int sx = (x - cx + r) * img.w / d;
			if (sx < 0) sx = 0;
			if (sx >= img.w) sx = img.w - 1;
			uint16_t c = srcRow[sx];
			if (opa >= LV_OPA_COVER)
				row[x] = c;
			else
				row[x] = blend565(row[x], c, opa);
		}
	}
}

// ============================================================
// 竹叶 blit — 逆旋转 + 最近邻采样 + 逐像素 alpha blend（白色背景已抠除）
// ============================================================

// 通用旋转精灵绘制：逆旋转 + 最近邻采样 + 逐像素 alpha*fadeByte 混合
void FruitNinjaRenderer::blitRotatedSprite(const FruitImage& img, const uint8_t* alpha,
	float cx, float cy, float scale, float angle, uint8_t fadeByte)
{
	if (!m_buf || !img.valid || !alpha) return;
	const int iw = img.w, ih = img.h;
	const float ca = cosf(angle), sa = sinf(angle);
	// ⚠ 居中旋转：逆旋转须绕源图中心 (iw/2, ih/2) 采样。
	// 旧代码少了 +源中心 偏移 → 精灵被推到基准点右上侧，超出半对角包围框被裁剪（人物只剩右上角 1/4）。
	const float cxS = iw * 0.5f;
	const float cyS = ih * 0.5f;

	// 旋转后外接范围（半对角近似）
	float hr = 0.5f * scale * hypotf((float)iw, (float)ih);
	int x1 = (int)(cx - hr), y1 = (int)(cy - hr);
	int x2 = (int)(cx + hr), y2 = (int)(cy + hr);
	if (x2 < 0 || y2 < 0 || x1 >= W || y1 >= H) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= W) x2 = W - 1;
	if (y2 >= H) y2 = H - 1;

	const float invScale = 1.0f / scale;
	for (int y = y1; y <= y2; y++)
	{
		float ry = (y - cy) * invScale;
		uint16_t* row = reinterpret_cast<uint16_t*>(m_buf + (size_t)y * m_stride);
		for (int x = x1; x <= x2; x++)
		{
			float rx = (x - cx) * invScale;
			// 逆旋转（-angle）：绕源图中心采样，源坐标 = 源中心 + R(-θ)·(dest/scale)
			float sx = cxS + (ca * rx + sa * ry);
			float sy = cyS + (-sa * rx + ca * ry);
			int si = (int)sx, sj = (int)sy;
			if (si < 0 || sj < 0 || si >= iw || sj >= ih) continue;
			uint8_t a = alpha[(size_t)sj * iw + si];
			if (a < 8) continue;   // 近透明像素跳过
			if (fadeByte < 255)
			{
				a = (uint8_t)(((uint32_t)a * fadeByte) >> 8);   // 应用整体渐隐
				if (a < 8) continue;
			}
			uint16_t src = img.buf[(size_t)sj * iw + si];
			row[x] = blend565(row[x], src, a);
		}
	}
}

void FruitNinjaRenderer::blitLeaf(float cx, float cy, const LeafParticle& l)
{
	if (!m_leafImg.valid || !m_leafAlpha) return;
	// 渐隐：前 50% 寿命保持不透明，后 50% 线性淡出（按 maxLife 比例）
	float frac = (l.maxLife > 0.0f) ? (l.life / l.maxLife) : 1.0f;
	float fade = (frac > 0.5f) ? 1.0f : (frac * 2.0f);
	if (fade <= 0.0f) return;
	uint8_t fadeByte = (uint8_t)(fade * 255.0f);
	blitRotatedSprite(m_leafImg, m_leafAlpha, cx, cy, l.scale, l.angle, fadeByte);
}

void FruitNinjaRenderer::paintLeaves()
{
	for (int i = 0; i < kMaxLeaves; i++)
	{
		const auto& l = m_leaves[i];
		if (!m_redrawLeaf[i] || !l.active) continue;
		blitLeaf(l.x, l.y, l);
	}
}

void FruitNinjaRenderer::blitQiuyuan(float cx, float cy, float angle)
{
	if (!m_qiuyuanImg.valid || !m_qiuyuanAlpha) return;
	// 人物按固定显示高度等比缩放（保持比例）
	float scale = kQiuyuanHeight / (float)m_qiuyuanImg.h;
	blitRotatedSprite(m_qiuyuanImg, m_qiuyuanAlpha, cx, cy, scale, angle, 255);
}

void FruitNinjaRenderer::paintTouchHeads()
{
	for (int i = 0; i < kMaxTouchHeads; i++)
	{
		const auto& h = m_touchHeads[i];
		if (!m_redrawHead[i] || !h.active) continue;
		float angle = atan2f(h.dirY, h.dirX);   // 沿滑动方向旋转
		blitQiuyuan(h.x, h.y, angle);
	}
}

void FruitNinjaRenderer::paintFruit(const FruitNinjaLogic::Fruit& f)
{
	float r = FruitNinjaLogic::getFruitRadius(f.type);
	int idx = static_cast<int>(f.type);
	const FruitImage& img = (idx >= 0 && idx < 5) ? m_fruitImgs[idx] : s_emptyImg;
	if (f.sliced)
	{
		// 缩小一半圆 + 淡出（左上角对齐，与旧视觉一致）
		int diam = static_cast<int>(r);
		int cx = static_cast<int>(f.x) - static_cast<int>(r) + diam / 2;
		int cy = static_cast<int>(f.y) - static_cast<int>(r) + diam / 2;
		if (img.valid)
			blitImage(cx, cy, diam / 2, img, f.opacity);
		else
			fillCircle(cx, cy, diam / 2, fruitFillColor(f.type), f.opacity);
	}
	else
	{
		if (img.valid)
			blitImage(static_cast<int>(f.x), static_cast<int>(f.y), static_cast<int>(r), img, LV_OPA_COVER);
		else
			fillCircle(static_cast<int>(f.x), static_cast<int>(f.y), static_cast<int>(r), fruitFillColor(f.type), LV_OPA_COVER);
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
	for (int i = 0; i < MaxFruits; i++)
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
	for (int i = 0; i < MaxParticles; i++)
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

	// ── 竹叶（旋转/缩放/渐隐逐帧变化 → 活跃即每帧标脏重绘） ──
	for (int i = 0; i < kMaxLeaves; i++)
	{
		BBox cur = m_leaves[i].active ? leafBBox(m_leaves[i]) : BBox{};
		BBox& prev = m_prevLeaf[i];
		bool changed = (cur.active != prev.active);
		if (cur.active) changed = true;   // 竹叶有动画 → 每帧重绘（旋转角度不改变 bbox 也须刷新）
		if (changed)
		{
			if (prev.active) dirtyAdd(prev.x1, prev.y1, prev.x2, prev.y2);
			if (cur.active) dirtyAdd(cur.x1, cur.y1, cur.x2, cur.y2);
		}
		prev = cur;
	}

	// ── 触摸头部人物（跟随手指/旋转 → 活跃即每帧标脏重绘） ──
	for (int i = 0; i < kMaxTouchHeads; i++)
	{
		BBox cur = m_touchHeads[i].active ? headBBox(m_touchHeads[i]) : BBox{};
		BBox& prev = m_prevHead[i];
		bool changed = (cur.active != prev.active);
		if (cur.active) changed = true;   // 人物移动/旋转动画 → 每帧重绘
		if (changed)
		{
			if (prev.active) dirtyAdd(prev.x1, prev.y1, prev.x2, prev.y2);
			if (cur.active) dirtyAdd(cur.x1, cur.y1, cur.x2, cur.y2);
		}
		prev = cur;
	}

	// ── 2P 分割线：首帧强制标脏，保证初始即绘制（否则只有被水果/刀光覆盖时才补画，导致分割线缺失）──
	if (logic.getPlayerCount() >= 2 && !m_dividerPainted)
		dirtyAdd(W / 2 - 1, 0, W / 2, H - 1);
}

void FruitNinjaRenderer::expandRedraw(const FruitNinjaLogic& logic)
{
	memset(m_redrawFruit, 0, sizeof(m_redrawFruit));
	memset(m_redrawParticle, 0, sizeof(m_redrawParticle));
	memset(m_redrawSeg, 0, sizeof(m_redrawSeg));
	memset(m_redrawLeaf, 0, sizeof(m_redrawLeaf));
	memset(m_redrawHead, 0, sizeof(m_redrawHead));
	m_redrawDivider = false;

	// 重画集 = 与脏区集合 D 相交的 active 对象（一次遍历，无 O(n²) 传播）。
	// 正确性：清 D 时被清除影响的对象，其 bbox 必然与 D 相交 → 进 R 重画；
	// paint 按 z 序从底到顶绘制，上层对象重画自然覆盖下层，被覆盖的下层无需单独重画。
	const auto* fruits = logic.getFruits();
	const auto* particles = logic.getParticles();
	for (int i = 0; i < MaxFruits; i++)
		if (fruits[i].active && dirtyIntersects(fruitBBox(fruits[i])))
			m_redrawFruit[i] = true;
	for (int i = 0; i < MaxParticles; i++)
		if (particles[i].active && dirtyIntersects(particleBBox(particles[i])))
			m_redrawParticle[i] = true;
	for (int i = 0; i < kMaxSegments; i++)
		if (m_segments[i].active && dirtyIntersects(segBBox(m_segments[i])))
			m_redrawSeg[i] = true;
	for (int i = 0; i < kMaxLeaves; i++)
		if (m_leaves[i].active && dirtyIntersects(leafBBox(m_leaves[i])))
			m_redrawLeaf[i] = true;
	for (int i = 0; i < kMaxTouchHeads; i++)
		if (m_touchHeads[i].active && dirtyIntersects(headBBox(m_touchHeads[i])))
			m_redrawHead[i] = true;
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
	// 1. 清 D 各区域背景（有背景图则从图复制，否则纯色）
	for (int i = 0; i < m_dirtyCount; i++)
	{
		const auto& d = m_dirty[i];
		if (m_bg.valid)
			paintBgRect(d.x1, d.y1, d.x2, d.y2);
		else
			fillRect(d.x1, d.y1, d.x2, d.y2, m_bg565);
	}

	// 2. 按 z 序重画 R：水果 → 粒子 → 分隔线 → 刀光 → 竹叶 → 提示文字
	for (int i = 0; i < MaxFruits; i++)
		if (m_redrawFruit[i])
			paintFruit(logic.getFruits()[i]);
	for (int i = 0; i < MaxParticles; i++)
		if (m_redrawParticle[i])
			paintParticle(logic.getParticles()[i]);
	if (m_redrawDivider)
	{
		paintDivider();
		m_dividerPainted = true;   // 分割线已初始绘制完成
	}
	for (int i = 0; i < kMaxSegments; i++)
		if (m_redrawSeg[i])
			drawWideLine(m_segments[i].x1, m_segments[i].y1,
				m_segments[i].x2, m_segments[i].y2, 14, color565(kBambooGreen));   // 竹绿色拖尾
	paintLeaves();
	paintTouchHeads();
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
	// 水果显示"方向+按钮"；炸弹也显示方向箭头（红色，提示危险方向，任意键引爆）
	return f.active && !f.sliced;
}

void FruitNinjaRenderer::makeHintText(const FruitNinjaLogic::Fruit& f, char* out, size_t cap)
{
	// 炸弹只显示方向箭头（任意按钮引爆，无特定按钮提示）
	if (f.type == FruitNinjaLogic::FruitType::Bomb)
	{
		snprintf(out, cap, "%s", FruitNinjaLogic::dirToArrow(f.requiredDir));
	}
	else
	{
		snprintf(out, cap, "%s%s",
			FruitNinjaLogic::dirToArrow(f.requiredDir),
			FruitNinjaLogic::btnToChar(f.requiredBtn));
	}
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

void FruitNinjaRenderer::loadFruitImages()
{
	// 路径按 FruitType 索引：Watermelon/Apple/Orange/Banana/Bomb
	static const char* paths[5] = {
		"/root/system/fruitNinja/watermelon.jpg",
		"/root/system/fruitNinja/apple.jpg",
		"/root/system/fruitNinja/orange.jpg",
		"/root/system/fruitNinja/banana.jpg",
		"/root/system/fruitNinja/bomb.jpg",
	};

	jpeg_decode_engine_cfg_t eng_cfg = { .intr_priority = 0, .timeout_ms = 500 };
	jpeg_decoder_handle_t eng = nullptr;
	if (jpeg_new_decoder_engine(&eng_cfg, &eng) != ESP_OK)
	{
		ESP_LOGE(TAG, "jpeg_new_decoder_engine 失败，水果将回退纯色圆");
		return;
	}

	constexpr int MAX_SIDE = 128;   // 源图最大边（水果显示直径 ≤~90px，128 足够清晰）

	for (int i = 0; i < 5; i++)
	{
		FILE* f = fopen(paths[i], "rb");
		if (!f)
		{
			ESP_LOGW(TAG, "打开水果图片失败: %s", paths[i]);
			continue;
		}
		fseek(f, 0, SEEK_END);
		long fsz = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (fsz <= 0)
		{
			fclose(f);
			continue;
		}
		// 输入/输出 buffer 必须用驱动专用 jpeg_alloc_decoder_mem 分配（硬件 DMA 对齐要求）
		jpeg_decode_memory_alloc_cfg_t inMem = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
		size_t inAlloc = 0;
		uint8_t* jpg = static_cast<uint8_t*>(jpeg_alloc_decoder_mem((size_t)fsz, &inMem, &inAlloc));
		if (!jpg)
		{
			fclose(f);
			continue;
		}
		if (fread(jpg, 1, (size_t)fsz, f) != (size_t)fsz)
		{
			heap_caps_free(jpg);
			fclose(f);
			continue;
		}
		fclose(f);

		jpeg_decode_picture_info_t info{};
		if (jpeg_decoder_get_info(jpg, (uint32_t)fsz, &info) != ESP_OK || info.width == 0 || info.height == 0)
		{
			heap_caps_free(jpg);
			ESP_LOGW(TAG, "JPEG 头解析失败: %s", paths[i]);
			continue;
		}

		size_t fullSize = (size_t)info.width * info.height * 2;
		jpeg_decode_memory_alloc_cfg_t outMem = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
		size_t outAlloc = 0;
		uint8_t* full = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(fullSize, &outMem, &outAlloc));
		if (!full)
		{
			heap_caps_free(jpg);
			continue;
		}
		jpeg_decode_cfg_t dec_cfg = {
			.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
			.rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   // 小端，可直接 uint16 读（与 canvas 一致）
			.conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
		};
		uint32_t outSize = 0;
		uint32_t outBufSize = (outAlloc > 0 && outAlloc >= fullSize) ? (uint32_t)outAlloc : (uint32_t)fullSize;
		if (jpeg_decoder_process(eng, &dec_cfg, jpg, (uint32_t)fsz, full, outBufSize, &outSize) != ESP_OK)
		{
			heap_caps_free(full);
			heap_caps_free(jpg);
			ESP_LOGW(TAG, "JPEG 解码失败: %s", paths[i]);
			continue;
		}
		heap_caps_free(jpg);

		// 最近邻缩放到最大边 ≤ MAX_SIDE：控制内存 + 加速 blit
		int sw = info.width, sh = info.height;
		if (sw > MAX_SIDE || sh > MAX_SIDE)
		{
			float k = (float)MAX_SIDE / (sw > sh ? sw : sh);
			int tw = (int)(sw * k); if (tw < 1) tw = 1;
			int th = (int)(sh * k); if (th < 1) th = 1;
			uint16_t* small = static_cast<uint16_t*>(heap_caps_malloc((size_t)tw * th * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
			if (small)
			{
				const uint16_t* src = reinterpret_cast<const uint16_t*>(full);
				for (int y = 0; y < th; y++)
				{
					int sy = (y * sh) / th;
					for (int x = 0; x < tw; x++)
					{
						int sx = (x * sw) / tw;
						small[(size_t)y * tw + x] = src[(size_t)sy * sw + sx];
					}
				}
				heap_caps_free(full);
				full = reinterpret_cast<uint8_t*>(small);
				sw = tw; sh = th;
			}
		}

		auto& img = m_fruitImgs[i];
		img.buf = reinterpret_cast<const uint16_t*>(full);
		img.w = sw; img.h = sh;
		img.valid = true;
		ESP_LOGI(TAG, "水果图片 %d 就绪: %dx%d", i, sw, sh);
	}

	jpeg_del_decoder_engine(eng);
}

// ============================================================
// 全屏背景图 — 硬件解码（直接 1280×720 同尺寸，一次加载，逐帧仅按区域 memcpy）
// ============================================================

void FruitNinjaRenderer::loadBackground()
{
	static const char* path = "/root/system/fruitNinja/fruitbackground.jpg";

	FILE* f = fopen(path, "rb");
	if (!f)
	{
		ESP_LOGW(TAG, "打开背景图失败: %s", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	long fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsz <= 0)
	{
		fclose(f);
		return;
	}

	jpeg_decode_memory_alloc_cfg_t inMem = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
	size_t inAlloc = 0;
	uint8_t* jpg = static_cast<uint8_t*>(jpeg_alloc_decoder_mem((size_t)fsz, &inMem, &inAlloc));
	if (!jpg)
	{
		fclose(f);
		return;
	}
	if (fread(jpg, 1, (size_t)fsz, f) != (size_t)fsz)
	{
		heap_caps_free(jpg);
		fclose(f);
		return;
	}
	fclose(f);

	jpeg_decode_picture_info_t info{};
	if (jpeg_decoder_get_info(jpg, (uint32_t)fsz, &info) != ESP_OK || info.width == 0 || info.height == 0)
	{
		heap_caps_free(jpg);
		ESP_LOGW(TAG, "背景图 JPEG 头解析失败: %s", path);
		return;
	}

	// 背景图须与画布同尺寸（1280×720），直接 1:1 使用，无需缩放/对齐。
	if (info.width != W || info.height != H)
	{
		heap_caps_free(jpg);
		ESP_LOGW(TAG, "背景图尺寸 %dx%d 与画布 %dx%d 不符，回退纯色", info.width, info.height, W, H);
		return;
	}

	size_t fullSize = (size_t)info.width * info.height * 2;
	jpeg_decode_memory_alloc_cfg_t outMem = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
	size_t outAlloc = 0;
	uint8_t* full = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(fullSize, &outMem, &outAlloc));
	if (!full)
	{
		heap_caps_free(jpg);
		return;
	}

	jpeg_decode_engine_cfg_t eng_cfg = { .intr_priority = 0, .timeout_ms = 500 };
	jpeg_decoder_handle_t eng = nullptr;
	if (jpeg_new_decoder_engine(&eng_cfg, &eng) != ESP_OK)
	{
		heap_caps_free(full);
		heap_caps_free(jpg);
		ESP_LOGE(TAG, "jpeg_new_decoder_engine 失败，背景回退纯色");
		return;
	}
	jpeg_decode_cfg_t dec_cfg = {
		.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
		.rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   // 小端，可直接 uint16 读（与 canvas 一致）
		.conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
	};
	uint32_t outSize = 0;
	uint32_t outBufSize = (outAlloc >= fullSize) ? (uint32_t)outAlloc : (uint32_t)fullSize;
	if (jpeg_decoder_process(eng, &dec_cfg, jpg, (uint32_t)fsz, full, outBufSize, &outSize) != ESP_OK)
	{
		heap_caps_free(full);
		heap_caps_free(jpg);
		jpeg_del_decoder_engine(eng);
		ESP_LOGW(TAG, "背景图解码失败: %s", path);
		return;
	}
	heap_caps_free(jpg);
	jpeg_del_decoder_engine(eng);

	m_bg.buf = reinterpret_cast<const uint16_t*>(full);
	m_bg.w = W;
	m_bg.h = H;
	m_bg.valid = true;
	ESP_LOGI(TAG, "背景图就绪: %dx%d", info.width, info.height);
}

// ============================================================
// 竹叶精灵图 — 硬件解码 + 白色背景抠图 alpha（失败回退不生成竹叶）
// ============================================================

void FruitNinjaRenderer::loadLeafImage()
{
	static const char* path = "/root/system/fruitNinja/bamboo leaves.jpg";

	FILE* f = fopen(path, "rb");
	if (!f)
	{
		ESP_LOGW(TAG, "打开竹叶图失败: %s", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	long fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsz <= 0)
	{
		fclose(f);
		return;
	}

	// 输入 buffer 必须用驱动专用 jpeg_alloc_decoder_mem 分配（硬件 DMA 对齐要求）
	jpeg_decode_memory_alloc_cfg_t inMem = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
	size_t inAlloc = 0;
	uint8_t* jpg = static_cast<uint8_t*>(jpeg_alloc_decoder_mem((size_t)fsz, &inMem, &inAlloc));
	if (!jpg)
	{
		fclose(f);
		return;
	}
	if (fread(jpg, 1, (size_t)fsz, f) != (size_t)fsz)
	{
		heap_caps_free(jpg);
		fclose(f);
		return;
	}
	fclose(f);

	jpeg_decode_picture_info_t info{};
	if (jpeg_decoder_get_info(jpg, (uint32_t)fsz, &info) != ESP_OK || info.width == 0 || info.height == 0)
	{
		heap_caps_free(jpg);
		ESP_LOGW(TAG, "竹叶图 JPEG 头解析失败: %s", path);
		return;
	}

	// 解码器输出按 MCU 对齐（jpeg_parse_sof_marker）：process_h/process_v 是宽高的 MCU 倍数。
	// YUV444/GRAY: MCU 8×8（8 像素对齐）；YUV422: 16×8；YUV420: 16×16。
	int mcux = (info.sample_method == JPEG_DOWN_SAMPLING_YUV444 || info.sample_method == JPEG_DOWN_SAMPLING_GRAY) ? 8 : 16;
	int mcuy = (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) ? 16 : 8;
	int processW = (int)(((uint32_t)info.width + mcux - 1) & ~(uint32_t)(mcux - 1));
	int processH = (int)(((uint32_t)info.height + mcuy - 1) & ~(uint32_t)(mcuy - 1));
	size_t fullSize = (size_t)processW * processH * 2;
	jpeg_decode_memory_alloc_cfg_t outMem = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
	size_t outAlloc = 0;
	uint8_t* full = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(fullSize, &outMem, &outAlloc));
	if (!full)
	{
		heap_caps_free(jpg);
		return;
	}

	jpeg_decode_engine_cfg_t eng_cfg = { .intr_priority = 0, .timeout_ms = 500 };
	jpeg_decoder_handle_t eng = nullptr;
	if (jpeg_new_decoder_engine(&eng_cfg, &eng) != ESP_OK)
	{
		heap_caps_free(full);
		heap_caps_free(jpg);
		ESP_LOGE(TAG, "jpeg_new_decoder_engine 失败，竹叶特效禁用");
		return;
	}
	jpeg_decode_cfg_t dec_cfg = {
		.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
		.rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   // 小端，可直接 uint16 读（与 canvas 一致）
		.conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
	};
	uint32_t outSize = 0;
	uint32_t outBufSize = (outAlloc >= fullSize) ? (uint32_t)outAlloc : (uint32_t)fullSize;
	if (jpeg_decoder_process(eng, &dec_cfg, jpg, (uint32_t)fsz, full, outBufSize, &outSize) != ESP_OK)
	{
		heap_caps_free(full);
		heap_caps_free(jpg);
		jpeg_del_decoder_engine(eng);
		ESP_LOGW(TAG, "竹叶图解码失败: %s", path);
		return;
	}
	heap_caps_free(jpg);
	jpeg_del_decoder_engine(eng);

	// 最近邻缩放到最大边 ≤128 + 统一重排为紧凑 RGB565（行 stride = sw）。
	// ⚠ 回读行距必须用解码器真实 processW（MCU 对齐）：非 16 对齐宽度用 info.width 当行距会逐行错位斜切。
	int sw = info.width, sh = info.height;
	constexpr int MAX_SIDE = 128;
	if (sw > MAX_SIDE || sh > MAX_SIDE)
	{
		float k = (float)MAX_SIDE / (sw > sh ? sw : sh);
		sw = (int)(sw * k); if (sw < 1) sw = 1;
		sh = (int)(sh * k); if (sh < 1) sh = 1;
	}
	uint16_t* small = static_cast<uint16_t*>(heap_caps_malloc((size_t)sw * sh * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if (!small)
	{
		heap_caps_free(full);
		ESP_LOGW(TAG, "竹叶 重排分配失败，特效禁用");
		return;
	}
	{
		const uint16_t* src = reinterpret_cast<const uint16_t*>(full);
		for (int y = 0; y < sh; y++)
		{
			int sy = (y * (int)info.height) / sh;
			for (int x = 0; x < sw; x++)
			{
				int sx = (x * (int)info.width) / sw;
				small[(size_t)y * sw + x] = src[(size_t)sy * processW + sx];
			}
		}
	}
	heap_caps_free(full);
	full = reinterpret_cast<uint8_t*>(small);

	// 白色/浅色背景抠图 → 逐像素 alpha（PSRAM 独立平面）
	uint8_t* alpha = static_cast<uint8_t*>(heap_caps_malloc((size_t)sw * sh, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if (!alpha)
	{
		heap_caps_free(full);
		ESP_LOGW(TAG, "竹叶 alpha 分配失败，特效禁用");
		return;
	}
	const uint16_t* src = reinterpret_cast<const uint16_t*>(full);
	for (int y = 0; y < sh; y++)
	{
		for (int x = 0; x < sw; x++)
		{
			uint16_t c = src[(size_t)y * sw + x];
			// RGB565 → 8bit 分量，取最大通道作为亮度（白色=高亮）
			int r = (c >> 11) & 0x1f; r = (r << 3) | (r >> 2);
			int g = (c >> 5) & 0x3f;  g = (g << 2) | (g >> 6);
			int b = c & 0x1f;         b = (b << 3) | (b >> 2);
			int bright = r > g ? (r > b ? r : b) : (g > b ? g : b);
			int a;
			if (bright >= 240)      a = 0;                            // 近白 → 全透明
			else if (bright >= 200) a = (240 - bright) * 255 / 40;    // 过渡带渐隐（平滑边缘）
			else                    a = 255;                          // 叶片 → 不透明
			alpha[(size_t)y * sw + x] = (uint8_t)a;
		}
	}

	m_leafImg.buf = reinterpret_cast<const uint16_t*>(full);
	m_leafImg.w = sw;
	m_leafImg.h = sh;
	m_leafImg.valid = true;
	m_leafAlpha = alpha;
	ESP_LOGI(TAG, "竹叶图就绪: %dx%d, alpha 抠图完成", sw, sh);
}

// ============================================================
// qiuyuan 人物精灵图 — 硬件解码 + 16 像素对齐 + 白色背景抠图（失败回退不显示人物）
// ============================================================

void FruitNinjaRenderer::loadQiuyuanImage()
{
	static const char* path = "/root/system/fruitNinja/qiuyuan.jpg";

	FILE* f = fopen(path, "rb");
	if (!f)
	{
		ESP_LOGW(TAG, "打开 qiuyuan 图失败: %s", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	long fsz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsz <= 0)
	{
		fclose(f);
		return;
	}

	// 输入 buffer 必须用驱动专用 jpeg_alloc_decoder_mem 分配（硬件 DMA 对齐要求）
	jpeg_decode_memory_alloc_cfg_t inMem = { .buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER };
	size_t inAlloc = 0;
	uint8_t* jpg = static_cast<uint8_t*>(jpeg_alloc_decoder_mem((size_t)fsz, &inMem, &inAlloc));
	if (!jpg)
	{
		fclose(f);
		return;
	}
	if (fread(jpg, 1, (size_t)fsz, f) != (size_t)fsz)
	{
		heap_caps_free(jpg);
		fclose(f);
		return;
	}
	fclose(f);

	jpeg_decode_picture_info_t info{};
	if (jpeg_decoder_get_info(jpg, (uint32_t)fsz, &info) != ESP_OK || info.width == 0 || info.height == 0)
	{
		heap_caps_free(jpg);
		ESP_LOGW(TAG, "qiuyuan JPEG 头解析失败: %s", path);
		return;
	}

	// 解码器输出按 MCU 对齐（jpeg_parse_sof_marker）：process_h/process_v 是宽高的 MCU 倍数。
	// YUV444/GRAY: MCU 8×8（8 像素对齐）；YUV422: 16×8；YUV420: 16×16。
	// ⚠ 必须用真实 processW 作回读行距：旧代码按 16 像素对齐，YUV444 且宽度 %16==8 时逐行错位斜切（人物只剩右下/旋转后右上 1/4）。
	int mcux = (info.sample_method == JPEG_DOWN_SAMPLING_YUV444 || info.sample_method == JPEG_DOWN_SAMPLING_GRAY) ? 8 : 16;
	int mcuy = (info.sample_method == JPEG_DOWN_SAMPLING_YUV420) ? 16 : 8;
	int processW = (int)(((uint32_t)info.width + mcux - 1) & ~(uint32_t)(mcux - 1));
	int processH = (int)(((uint32_t)info.height + mcuy - 1) & ~(uint32_t)(mcuy - 1));
	size_t fullSize = (size_t)processW * processH * 2;
	jpeg_decode_memory_alloc_cfg_t outMem = { .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER };
	size_t outAlloc = 0;
	uint8_t* full = static_cast<uint8_t*>(jpeg_alloc_decoder_mem(fullSize, &outMem, &outAlloc));
	if (!full)
	{
		heap_caps_free(jpg);
		return;
	}

	jpeg_decode_engine_cfg_t eng_cfg = { .intr_priority = 0, .timeout_ms = 500 };
	jpeg_decoder_handle_t eng = nullptr;
	if (jpeg_new_decoder_engine(&eng_cfg, &eng) != ESP_OK)
	{
		heap_caps_free(full);
		heap_caps_free(jpg);
		ESP_LOGE(TAG, "jpeg_new_decoder_engine 失败，qiuyuan 人物禁用");
		return;
	}
	jpeg_decode_cfg_t dec_cfg = {
		.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
		.rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,   // 小端，可直接 uint16 读（与 canvas 一致）
		.conv_std      = JPEG_YUV_RGB_CONV_STD_BT601,
	};
	uint32_t outSize = 0;
	uint32_t outBufSize = (outAlloc >= fullSize) ? (uint32_t)outAlloc : (uint32_t)fullSize;
	if (jpeg_decoder_process(eng, &dec_cfg, jpg, (uint32_t)fsz, full, outBufSize, &outSize) != ESP_OK)
	{
		heap_caps_free(full);
		heap_caps_free(jpg);
		jpeg_del_decoder_engine(eng);
		ESP_LOGW(TAG, "qiuyuan 解码失败: %s", path);
		return;
	}
	heap_caps_free(jpg);
	jpeg_del_decoder_engine(eng);

	// 诊断日志：确认原图尺寸/采样方式/解码输出尺寸是否与 processW×processH 一致。
	// ⚠ sample_method 是 COLOR_TYPE_ID 编码的大数值（如 YUV420=50331650），须用枚举名打印
	const char* sampleName = "?";
	switch (info.sample_method)
	{
	case JPEG_DOWN_SAMPLING_YUV444: sampleName = "YUV444"; break;
	case JPEG_DOWN_SAMPLING_YUV422: sampleName = "YUV422"; break;
	case JPEG_DOWN_SAMPLING_YUV420: sampleName = "YUV420"; break;
	case JPEG_DOWN_SAMPLING_GRAY:   sampleName = "GRAY";   break;
	default: break;
	}
	ESP_LOGI(TAG, "qiuyuan 原图 %dx%d sample=%s outSize=%u proc=%dx%d",
		info.width, info.height, sampleName, outSize, processW, processH);

	// 统一重排为紧凑 RGB565（无行填充）并缩到最大边 ≤256（控制内存 + 加速每帧 blit）
	constexpr int MAX_SIDE = 256;
	float k = 1.0f;
	if ((int)info.width > MAX_SIDE || (int)info.height > MAX_SIDE)
		k = (float)MAX_SIDE / ((int)info.width > (int)info.height ? (int)info.width : (int)info.height);
	int sw = (int)(info.width * k);  if (sw < 1) sw = 1;
	int sh = (int)(info.height * k); if (sh < 1) sh = 1;

	uint16_t* compact = static_cast<uint16_t*>(heap_caps_malloc((size_t)sw * sh * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if (!compact)
	{
		heap_caps_free(full);
		ESP_LOGW(TAG, "qiuyuan 重排分配失败，人物禁用");
		return;
	}
	const uint16_t* src = reinterpret_cast<const uint16_t*>(full);
	for (int y = 0; y < sh; y++)
	{
		int sy = (y * (int)info.height) / sh;
		for (int x = 0; x < sw; x++)
		{
			int sx = (x * (int)info.width) / sw;
			compact[(size_t)y * sw + x] = src[(size_t)sy * processW + sx];   // 源行 stride = processW（MCU 对齐）
		}
	}
	heap_caps_free(full);

	// 白色/浅色背景抠图 → 逐像素 alpha（PSRAM 独立平面）
	uint8_t* alpha = static_cast<uint8_t*>(heap_caps_malloc((size_t)sw * sh, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
	if (!alpha)
	{
		heap_caps_free(compact);
		ESP_LOGW(TAG, "qiuyuan alpha 分配失败，人物禁用");
		return;
	}
	for (int y = 0; y < sh; y++)
	{
		for (int x = 0; x < sw; x++)
		{
			uint16_t c = compact[(size_t)y * sw + x];
			// RGB565 → 8bit 分量
			int r = (c >> 11) & 0x1f; r = (r << 3) | (r >> 2);
			int g = (c >> 5) & 0x3f;  g = (g << 2) | (g >> 6);
			int b = c & 0x1f;         b = (b << 3) | (b >> 2);
			// 白色背景抠图：按“到纯白的距离”判透明——只抠真正接近白色的背景，
			// 保留浅色但带颜色的人物主体（肤色/浅色衣服）。旧版按最大通道亮度
			// （bright≥200 即抠）会把浅色主体也抠掉 → 人物缺块（“被切掉一部分”）。
			int dr = 255 - r, dg = 255 - g, db = 255 - b;
			int dist2 = dr * dr + dg * dg + db * db;
			int a;
			if (dist2 <= 1200)      a = 0;                            // 近纯白 → 全透明（背景）
			else if (dist2 < 3000)  a = (dist2 - 1200) * 255 / 1800;  // 过渡带渐隐（平滑边缘）
			else                    a = 255;                          // 有颜色的主体 → 不透明
			alpha[(size_t)y * sw + x] = (uint8_t)a;
		}
	}

	// 诊断：统计不透明像素包围盒，确认人物在 sprite 内的占比/位置（排查构图/抠图偏置）
	{
		int minX = sw, minY = sh, maxX = -1, maxY = -1;
		for (int y = 0; y < sh; y++)
			for (int x = 0; x < sw; x++)
				if (alpha[(size_t)y * sw + x] > 0)
				{
					if (x < minX) minX = x;
					if (x > maxX) maxX = x;
					if (y < minY) minY = y;
					if (y > maxY) maxY = y;
				}
		if (maxX >= 0)
			ESP_LOGI(TAG, "qiuyuan 不透明 bbox: (%d,%d)-(%d,%d) 占 %dx%d", minX, minY, maxX, maxY, sw, sh);
	}

	m_qiuyuanImg.buf = reinterpret_cast<const uint16_t*>(compact);
	m_qiuyuanImg.w = sw;
	m_qiuyuanImg.h = sh;
	m_qiuyuanImg.valid = true;
	m_qiuyuanAlpha = alpha;
	ESP_LOGI(TAG, "qiuyuan 人物图就绪: %dx%d, alpha 抠图完成", sw, sh);
}

// ============================================================
// 背景区域复制 — 从已 cover 缩放的背景缓冲 memcpy 到 canvas（清脏区用）
// ============================================================

void FruitNinjaRenderer::paintBgRect(int x1, int y1, int x2, int y2)
{
	if (!m_buf || !m_bg.valid) return;
	if (x2 < 0 || y2 < 0 || x1 >= W || y1 >= H) return;
	if (x1 < 0) x1 = 0;
	if (y1 < 0) y1 = 0;
	if (x2 >= W) x2 = W - 1;
	if (y2 >= H) y2 = H - 1;

	const int cw = x2 - x1 + 1;
	for (int y = y1; y <= y2; y++)
	{
		const uint16_t* src = m_bg.buf + (size_t)y * m_bg.w + x1;
		uint16_t* dst = reinterpret_cast<uint16_t*>(m_buf + (size_t)y * m_stride) + x1;
		memcpy(dst, src, (size_t)cw * 2);
	}
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
	for (int i = 0; i < MaxFruits; i++)
	{
		const auto& f = fruits[i];
		if (!shouldShowHint(f) || !m_redrawFruit[i]) continue;
		float r = FruitNinjaLogic::getFruitRadius(f.type);
		char buf[8];
		makeHintText(f, buf, sizeof(buf));
		// 炸弹方向提示用红色警示，水果用白色
		uint16_t hintColor = (f.type == FruitNinjaLogic::FruitType::Bomb)
			? color565(0xffff5252) : 0xffff;
		blitText(static_cast<int>(f.x - 16), static_cast<int>(f.y - r - 24), buf, hintColor);
	}
}

// ============================================================
// 每帧渲染
// ============================================================

void FruitNinjaRenderer::beginFrame(const FruitNinjaLogic& logic)
{
	// 纯计算，无 LVGL 调用：在无锁区执行，缩短持锁窗口
	if (logic.getState() != FruitNinjaLogic::State::Playing) return;
	collectDirty(logic);
	expandRedraw(logic);
}

void FruitNinjaRenderer::render(const FruitNinjaLogic& logic)
{
	auto state = logic.getState();
	bool isPlaying = (state == FruitNinjaLogic::State::Playing);

	// ── 游戏层（canvas 脏区重绘；collectDirty/expandRedraw 已在 beginFrame 无锁完成） ──
	if (isPlaying)
	{
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

void FruitNinjaRenderer::addTrailSegment(float x1, float y1, float x2, float y2, int player)
{
	// 长线段拆分为多段短段（每段 ≤ kMaxSegLen）。
	// drawWideLine 遍历整段包围盒逐像素做距离判断：短段包围盒贴近线段本身（遍历≈填充），
	// 长段包围盒面积远大于线宽区域（遍历/填充比 ≈ 长度/线宽），是长刀光卡顿的主因。
	float dx = x2 - x1, dy = y2 - y1;
	float len = sqrtf(dx * dx + dy * dy);
	int n = static_cast<int>(ceilf(len / kMaxSegLen));
	if (n < 1) n = 1;

	for (int s = 0; s < n; s++)
	{
		float t0 = static_cast<float>(s) / n;
		float t1 = static_cast<float>(s + 1) / n;
		auto& seg = m_segments[m_segWrite];
		seg.x1 = x1 + dx * t0;
		seg.y1 = y1 + dy * t0;
		seg.x2 = x1 + dx * t1;
		seg.y2 = y1 + dy * t1;
		seg.life = TRAIL_LIFETIME;   // 回绕覆盖旧段时重置生命周期
		seg.player = static_cast<uint8_t>(player);
		seg.active = true;
		m_segWrite = (m_segWrite + 1) % kMaxSegments;

		// 切割时随机飘散竹叶：沿刀光路径按概率生成（每段末端），向四周散开。
		// 仅在竹叶图加载成功时生效；概率 ~50% 控密度（每 40px 一段，快划自然更密）。
		if (m_leafImg.valid && (rand() & 1))
			spawnLeaf(seg.x2, seg.y2, dx, dy);
	}
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

	ageLeaves(dt);   // 竹叶物理同步老化（重力/旋转/生命）
}

// ============================================================
// 竹叶 — 物理老化（重力 + 位置/角度积分 + 生命递减）
// ============================================================

void FruitNinjaRenderer::ageLeaves(float dt)
{
	constexpr float GRAVITY = 900.0f;   // px/s² 重力加速度
	for (int i = 0; i < kMaxLeaves; i++)
	{
		auto& l = m_leaves[i];
		if (!l.active) continue;

		l.vy += GRAVITY * dt;
		l.x += l.vx * dt;
		l.y += l.vy * dt;
		l.angle += l.angVel * dt;
		l.life -= dt;
		if (l.life <= 0.0f)
		{
			l.active = false;   // 生命耗尽 → 下一帧 render 清除残留
			if (m_activeLeafCount > 0)
				m_activeLeafCount--;
		}
	}
}

// ============================================================
// 竹叶 — 生成（切割点附近随机向四周飘散，轮询池复用）
// ============================================================

void FruitNinjaRenderer::spawnLeaf(float x, float y, float segDx, float segDy)
{
	// 同时活跃竹叶数已达上限 → 跳过（不生成），防止竹叶过多导致渲染卡顿
	if (m_activeLeafCount >= kMaxActiveLeaves)
		return;

	bool prevActive = m_leaves[m_leafWrite].active;
	auto& leaf = m_leaves[m_leafWrite];
	m_leafWrite = (m_leafWrite + 1) % kMaxLeaves;

	leaf.active = true;
	leaf.x = x;
	leaf.y = y;
	// 随机向四周散开：沿刀光段方向 ±92° 锥形 + 向上偏置初速
	float dir = atan2f(segDy, segDx) + randFloat(-1.6f, 1.6f);
	float spd = randFloat(90.0f, 260.0f);
	leaf.vx = cosf(dir) * spd;
	leaf.vy = sinf(dir) * spd - randFloat(30.0f, 130.0f);
	leaf.angle = randFloat(0.0f, 6.283f);         // 随机初始旋转
	leaf.angVel = randFloat(-6.0f, 6.0f);         // 随机旋转速度
	leaf.scale = randFloat(0.5f, 1.2f);           // 随机大小
	leaf.life = randFloat(0.6f, 1.1f);            // 随机寿命
	leaf.maxLife = leaf.life;                     // 渐隐比例基准
	if (!prevActive)
		m_activeLeafCount++;
}

// ============================================================
// 白刃轨迹 — 清空（保留上帧标记，下一帧 render 清除 canvas 残留）
// ============================================================

void FruitNinjaRenderer::clearTrail()
{
	for (int i = 0; i < kMaxSegments; i++)
		m_segments[i].active = false;
	m_segWrite = 0;

	// 一并清空竹叶（非游戏态/前台复位时立即消除残留）
	for (int i = 0; i < kMaxLeaves; i++)
		m_leaves[i].active = false;
	m_leafWrite = 0;
	m_activeLeafCount = 0;
}

// ============================================================
// 白刃轨迹 — 遍历活跃段（残留 0.5s 内持续触发切割）
// ============================================================

void FruitNinjaRenderer::forEachTrailSegment(
	const std::function<void(float, float, float, float, int)>& cb) const
{
	for (int i = 0; i < kMaxSegments; i++)
	{
		const auto& s = m_segments[i];
		if (!s.active) continue;
		cb(s.x1, s.y1, s.x2, s.y2, s.player);
	}
}

// ============================================================
// 触摸头部人物 — 设置/清空（跟随手指，沿滑动方向旋转）
// ============================================================

void FruitNinjaRenderer::setTouchHead(int index, float x, float y, float dirX, float dirY, bool active)
{
	if (index < 0 || index >= kMaxTouchHeads) return;
	auto& h = m_touchHeads[index];
	h.active = active;
	if (!active) return;
	// 方向仅在移动量足够时更新（抖动抑制，保持上一朝向）
	float len2 = dirX * dirX + dirY * dirY;
	if (len2 >= 4.0f)
	{
		h.dirX = dirX;
		h.dirY = dirY;
	}
	// 人物不直接压在手指下：沿滑动反方向（身后）偏移 kQiuyuanTrail 跟随手运动，保证可见
	float len = sqrtf(h.dirX * h.dirX + h.dirY * h.dirY);
	float nx = (len > 0.01f) ? h.dirX / len : 0.0f;
	float ny = (len > 0.01f) ? h.dirY / len : -1.0f;   // 无方向（刚按下）默认朝上 → 人物在手指下方
	float px = x - nx * kQiuyuanTrail;
	float py = y - ny * kQiuyuanTrail;
	// 钳制到屏幕内：人物完整可见（半对角外接矩形不超出屏幕）
	float hr = (m_qiuyuanImg.valid)
		? 0.5f * (kQiuyuanHeight / (float)m_qiuyuanImg.h) * hypotf((float)m_qiuyuanImg.w, (float)m_qiuyuanImg.h)
		: 0.0f;
	h.x = (hr * 2.0f <= W) ? fminf(fmaxf(px, hr), (float)W - 1.0f - hr) : (float)W * 0.5f;
	h.y = (hr * 2.0f <= H) ? fminf(fmaxf(py, hr), (float)H - 1.0f - hr) : (float)H * 0.5f;
}

void FruitNinjaRenderer::clearTouchHeads()
{
	for (int i = 0; i < kMaxTouchHeads; i++)
		m_touchHeads[i].active = false;
}
