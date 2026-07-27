#include "luaDisplay.hpp"
#include "display/font.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <esp_log.h>

static constexpr char TAG[] = "LuaDisplay";

// 自定义 clamp（替代已废弃的 lv_clamp）
template<typename T>
static constexpr T clamp_val(T val, T min, T max) {
    return val < min ? min : (val > max ? max : val);
}

// ============================================================
//  颜色解析
// ============================================================

static lv_color_t parseColor(lua_State* L, int index)
{
    if (lua_isstring(L, index)) {
        const char* s = lua_tostring(L, index);
        if (s[0] == '#') {
            return lv_color_hex(strtol(s + 1, nullptr, 16));
        }
        // 尝试解析无 # 前缀的十六进制
        return lv_color_hex(strtol(s, nullptr, 16));
    }

    if (lua_isinteger(L, index)) {
        return lv_color_hex((uint32_t)lua_tointeger(L, index));
    }

    if (lua_istable(L, index)) {
        lua_getfield(L, index, "r"); int r = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, index, "g"); int g = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        lua_getfield(L, index, "b"); int b = (int)lua_tointeger(L, -1); lua_pop(L, 1);
        return lv_color_make(
            (uint8_t)clamp_val(r, 0, 255),
            (uint8_t)clamp_val(g, 0, 255),
            (uint8_t)clamp_val(b, 0, 255)
        );
    }

    return lv_color_black();
}

// ============================================================
//  上下文获取（通过 Lua registry）
// ============================================================

static constexpr const char* REGISTRY_KEY = "LUA_DISPLAY_CTX";

static LuaDisplayContext* getCtx(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    auto* ctx = (LuaDisplayContext*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

// ============================================================
//  fill_screen(color)
// ============================================================

static int l_display_fill_screen(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_color_t color = parseColor(L, 1);
    auto guard = ctx->display->lockGuard();
    lv_obj_set_style_bg_color(ctx->canvas, color, 0);
    lv_obj_set_style_bg_opa(ctx->canvas, LV_OPA_COVER, 0);
    return 0;
}

// ============================================================
//  fill_rect(x, y, w, h, color)
// ============================================================

static int l_display_fill_rect(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_coord_t x = (lv_coord_t)luaL_checkinteger(L, 1);
    lv_coord_t y = (lv_coord_t)luaL_checkinteger(L, 2);
    lv_coord_t w = (lv_coord_t)luaL_checkinteger(L, 3);
    lv_coord_t h = (lv_coord_t)luaL_checkinteger(L, 4);
    lv_color_t color = parseColor(L, 5);

    auto guard = ctx->display->lockGuard();
    lv_obj_t* rect = lv_obj_create(ctx->canvas);
    lv_obj_remove_style_all(rect);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_style_bg_color(rect, color, 0);
    lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rect, 0, 0);
    lv_obj_set_style_radius(rect, 0, 0);
    return 0;
}

// ============================================================
//  draw_rect(x, y, w, h, color) — 空心矩形边框
// ============================================================

static int l_display_draw_rect(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_coord_t x = (lv_coord_t)luaL_checkinteger(L, 1);
    lv_coord_t y = (lv_coord_t)luaL_checkinteger(L, 2);
    lv_coord_t w = (lv_coord_t)luaL_checkinteger(L, 3);
    lv_coord_t h = (lv_coord_t)luaL_checkinteger(L, 4);
    lv_color_t color = parseColor(L, 5);

    auto guard = ctx->display->lockGuard();
    lv_obj_t* rect = lv_obj_create(ctx->canvas);
    lv_obj_remove_style_all(rect);
    lv_obj_set_size(rect, w, h);
    lv_obj_set_pos(rect, x, y);
    lv_obj_set_style_bg_opa(rect, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rect, 2, 0);
    lv_obj_set_style_border_color(rect, color, 0);
    lv_obj_set_style_border_opa(rect, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rect, 0, 0);
    return 0;
}

// ============================================================
//  draw_text(x, y, text, size, color)
// ============================================================

static int l_display_draw_text(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_coord_t x = (lv_coord_t)luaL_checkinteger(L, 1);
    lv_coord_t y = (lv_coord_t)luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    int size = (int)luaL_optinteger(L, 4, (int)FontLoader::FontSize::Default);
    lv_color_t color = parseColor(L, 5);

    // 选择字体尺寸
    const lv_font_t* font = nullptr;
    if (size <= 24)        font = FontLoader::getDefault(FontLoader::FontSize::Small);
    else if (size <= 32)   font = FontLoader::getDefault(FontLoader::FontSize::Default);
    else                   font = FontLoader::getDefault(FontLoader::FontSize::Large);

    auto guard = ctx->display->lockGuard();
    lv_obj_t* label = lv_label_create(ctx->canvas);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return 0;
}

// ============================================================
//  draw_line(x0, y0, x1, y1, color)
// ============================================================

static int l_display_draw_line(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_coord_t x0 = (lv_coord_t)luaL_checkinteger(L, 1);
    lv_coord_t y0 = (lv_coord_t)luaL_checkinteger(L, 2);
    lv_coord_t x1 = (lv_coord_t)luaL_checkinteger(L, 3);
    lv_coord_t y1 = (lv_coord_t)luaL_checkinteger(L, 4);
    lv_color_t color = parseColor(L, 5);

    // 使用细长矩形模拟线段
    lv_coord_t dx = x1 - x0;
    lv_coord_t dy = y1 - y0;
    lv_coord_t len = 1;
    if (dx != 0 || dy != 0) {
        len = (lv_coord_t)sqrtf((float)(dx * dx + dy * dy));
    }

    auto guard = ctx->display->lockGuard();
    lv_obj_t* line = lv_obj_create(ctx->canvas);
    lv_obj_remove_style_all(line);
    lv_obj_set_size(line, len, 3);  // 3px 粗
    // 定位到中点，通过变换旋转
    lv_obj_set_pos(line, (x0 + x1 - len) / 2, (y0 + y1) / 2 - 1);
    lv_obj_set_style_bg_color(line, color, 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);

    // 计算旋转角度
    float angle = (float)(atan2f((float)dy, (float)dx) * 180.0f / M_PI);
    lv_obj_set_style_transform_rotation(line, (int32_t)(angle * 10.0f), 0);
    lv_obj_set_style_transform_pivot_x(line, 0, 0);
    lv_obj_set_style_transform_pivot_y(line, 1, 0);
    return 0;
}

// ============================================================
//  draw_circle(cx, cy, r, color)
// ============================================================

static int l_display_draw_circle(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_coord_t cx = (lv_coord_t)luaL_checkinteger(L, 1);
    lv_coord_t cy = (lv_coord_t)luaL_checkinteger(L, 2);
    lv_coord_t r  = (lv_coord_t)luaL_checkinteger(L, 3);
    lv_color_t color = parseColor(L, 4);

    auto guard = ctx->display->lockGuard();
    lv_obj_t* circle = lv_obj_create(ctx->canvas);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, r * 2, r * 2);
    lv_obj_set_pos(circle, cx - r, cy - r);
    lv_obj_set_style_bg_color(circle, color, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(circle, r, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    return 0;
}

// ============================================================
//  width() / height()
// ============================================================

static int l_display_width(lua_State* L)
{
    // 使用 LVGL 实际分辨率（已处理 ROTATE_90）
    lua_pushinteger(L, LV_HOR_RES);
    return 1;
}

static int l_display_height(lua_State* L)
{
    lua_pushinteger(L, LV_VER_RES);
    return 1;
}

// ============================================================
//  luaopen_display — 注册模块
// ============================================================

extern "C" int luaopen_display(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"fill_screen",  l_display_fill_screen},
        {"fill_rect",    l_display_fill_rect},
        {"draw_rect",    l_display_draw_rect},
        {"draw_text",    l_display_draw_text},
        {"draw_line",    l_display_draw_line},
        {"draw_circle",  l_display_draw_circle},
        {"width",        l_display_width},
        {"height",       l_display_height},
        {NULL, NULL}
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}
