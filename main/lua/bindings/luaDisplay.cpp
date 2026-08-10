#include "luaDisplay.hpp"
#include "display/font.hpp"
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

// 自定义 clamp（替代已废弃的 lv_clamp）
template<typename T>
static constexpr T clamp_val(T val, T min, T max) {
    return val < min ? min : (val > max ? max : val);
}

// ⚠ 绘制参数安全化（防恶意/错误 Lua 脚本）：
//   1. (int16_t) 截断：两个不同坐标可能映射到同一值 → 静态跳过误判 → 画面残留
//   2. 负/超大尺寸：LVGL 面积运算溢出（x+w 回绕）或调试版 assert
//   3. draw_line 的 dx*dx+dy*dy 在坐标差 >46340 时 int 溢出 → NaN
// 钳制到屏幕坐标（旋转后 1280×720）±4096 的安全区间：任意两个不同值截断后仍不同，
// 且坐标差 < 10000，平方远小于 INT_MAX。
static void sanitizeCoord(int& x, int& y)
{
    x = clamp_val(x, -4096, 1280 + 4096);
    y = clamp_val(y, -4096, 720 + 4096);
}
static void sanitizeSize(int& w, int& h)
{
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (w > 4096) w = 4096;
    if (h > 4096) h = 4096;
}

// ============================================================
//  颜色解析
// ============================================================

static lv_color_t parseColor(lua_State* L, int index)
{
    // ⚠ 必须先判整数：lua_isstring 对数字也返回 true（数字可隐式转字符串），
    // 若先走字符串分支，0xff0000 会被 tostring 成 "16711680" 再按 16 进制解析 → 颜色错误
    if (lua_isinteger(L, index)) {
        return lv_color_hex((uint32_t)lua_tointeger(L, index));
    }

    if (lua_type(L, index) == LUA_TSTRING) {
        const char* s = lua_tostring(L, index);
        if (s && s[0] == '#') {
            return lv_color_hex(strtol(s + 1, nullptr, 16));
        }
        // 尝试解析无 # 前缀的十六进制
        return lv_color_hex(strtol(s ? s : "", nullptr, 16));
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
//  对象池槽位管理（按帧内绘制序号复用 LVGL 对象）
// ============================================================

static uint16_t to565(lv_color_t c) { return lv_color_to_u16(c); }

enum SlotKind : uint8_t { SLOT_NONE = 0, SLOT_SHAPE = 1, SLOT_TEXT = 2 };

// 获取/复用槽位对象。静态（对象已存在且与上帧同 kind + 同参数）返回 nullptr → 跳过更新。
// 所有 LVGL 调用须在 Display::LockGuard 下（gameLoop 持锁调用 loop(dt)）。
static LuaDisplayContext::Slot* slotAcquire(LuaDisplayContext* ctx, uint8_t kind,
    int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, const char* text)
{
    int i = ctx->slotIndex;
    if (i >= LuaDisplayContext::MaxSlots) {
        ctx->slotIndex = LuaDisplayContext::MaxSlots;  // 钳制上限，防止回收循环越界
        return nullptr;   // 溢出兜底（罕见）
    }
    ctx->slotIndex++;

    auto& s = ctx->slots[i];
    bool same = s.obj && s.kind == kind && s.x == x && s.y == y
             && s.w == w && s.h == h && s.color == color;
    if (same && kind == SLOT_TEXT && text) {
        // 文本过长无法完整比对 → 视为变化
        if (strlen(text) >= sizeof(s.text) - 1) same = false;
        else same = (strcmp(s.text, text) == 0);
    }
    if (same) return nullptr;   // 静态跳过

    // kind 变化（shape↔text）需重建对象
    if (!s.obj || s.kind != kind) {
        if (s.obj) lv_obj_delete(s.obj);
        s.obj = (kind == SLOT_TEXT) ? lv_label_create(ctx->canvas) : lv_obj_create(ctx->canvas);
    }
    s.kind = kind;
    s.x = x; s.y = y; s.w = w; s.h = h;
    s.color = color;
    if (text) {
        strncpy(s.text, text, sizeof(s.text) - 1);
        s.text[sizeof(s.text) - 1] = '\0';
    }
    return &s;
}

// ============================================================
//  fill_screen(color)
// ============================================================

static int l_display_fill_screen(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    lv_color_t color = parseColor(L, 1);
    uint16_t c = to565(color);
    // 背景色未变 → 跳过（避免每帧样式应用 + 整容器失效；屏幕已有该背景）
    if (ctx->bgSet && ctx->bg565 == c) return 0;
    ctx->bg565 = c;
    ctx->bgSet = true;
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

    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    lv_color_t color = parseColor(L, 5);

    sanitizeCoord(x, y);
    sanitizeSize(w, h);

    auto* s = slotAcquire(ctx, SLOT_SHAPE, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, to565(color), nullptr);
    if (!s) return 0;   // 静态跳过
    lv_obj_t* o = s->obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return 0;
}

// ============================================================
//  draw_rect(x, y, w, h, color) — 空心矩形边框
// ============================================================

static int l_display_draw_rect(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    lv_color_t color = parseColor(L, 5);

    sanitizeCoord(x, y);
    sanitizeSize(w, h);

    auto* s = slotAcquire(ctx, SLOT_SHAPE, (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h, to565(color), nullptr);
    if (!s) return 0;
    lv_obj_t* o = s->obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    lv_obj_set_style_border_color(o, color, 0);
    lv_obj_set_style_border_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return 0;
}

// ============================================================
//  draw_text(x, y, text, size, color)
// ============================================================

static int l_display_draw_text(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    int size = (int)luaL_optinteger(L, 4, (int)FontLoader::FontSize::Default);
    lv_color_t color = parseColor(L, 5);

    sanitizeCoord(x, y);

    // 选择字体尺寸
    const lv_font_t* font = nullptr;
    if (size <= 24)        font = FontLoader::getDefault(FontLoader::FontSize::Small);
    else if (size <= 32)   font = FontLoader::getDefault(FontLoader::FontSize::Default);
    else                   font = FontLoader::getDefault(FontLoader::FontSize::Large);
    if (!font) return 0;

    // 文本槽位：静态（同位置同文本同色）跳过 → 不触发 LVGL 重绘
    auto* s = slotAcquire(ctx, SLOT_TEXT, (int16_t)x, (int16_t)y, 0, 0, to565(color), text);
    if (!s) return 0;
    lv_obj_t* o = s->obj;
    lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, color, 0);
    return 0;
}

// ============================================================
//  draw_line(x0, y0, x1, y1, color)
// ============================================================

static int l_display_draw_line(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    int x0 = (int)luaL_checkinteger(L, 1);
    int y0 = (int)luaL_checkinteger(L, 2);
    int x1 = (int)luaL_checkinteger(L, 3);
    int y1 = (int)luaL_checkinteger(L, 4);
    lv_color_t color = parseColor(L, 5);

    sanitizeCoord(x0, y0);
    sanitizeCoord(x1, y1);

    // 槽位用包围盒（静态线跳过，不触发重绘）
    int bx = std::min(x0, x1), by = std::min(y0, y1);
    int bw = std::max(x0, x1) - bx + 1, bh = std::max(y0, y1) - by + 1;
    auto* s = slotAcquire(ctx, SLOT_SHAPE, (int16_t)bx, (int16_t)by, (int16_t)bw, (int16_t)bh, to565(color), nullptr);
    if (!s) return 0;
    lv_obj_t* o = s->obj;
    // 细长矩形 + transform 旋转（仅在线条变化时更新）
    lv_coord_t dx = x1 - x0, dy = y1 - y0;
    lv_coord_t len = (dx != 0 || dy != 0) ? (lv_coord_t)sqrtf((float)(dx * dx + dy * dy)) : 1;
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, len, 3);
    lv_obj_set_pos(o, (x0 + x1 - len) / 2, (y0 + y1) / 2 - 1);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    float angle = (float)(atan2f((float)dy, (float)dx) * 180.0f / M_PI);
    lv_obj_set_style_transform_rotation(o, (int32_t)(angle * 10.0f), 0);
    lv_obj_set_style_transform_pivot_x(o, 0, 0);
    lv_obj_set_style_transform_pivot_y(o, 1, 0);
    return 0;
}

// ============================================================
//  draw_circle(cx, cy, r, color)
// ============================================================

static int l_display_draw_circle(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx || !ctx->canvas) return 0;

    int cx = (int)luaL_checkinteger(L, 1);
    int cy = (int)luaL_checkinteger(L, 2);
    int r  = (int)luaL_checkinteger(L, 3);
    lv_color_t color = parseColor(L, 4);

    sanitizeCoord(cx, cy);
    if (r < 0) r = 0;
    if (r > 2048) r = 2048;   // ⚠ r*2 超 int16 会截断碰撞；2048 上限足够覆盖画布

    auto* s = slotAcquire(ctx, SLOT_SHAPE, (int16_t)(cx - r), (int16_t)(cy - r),
                          (int16_t)(r * 2), (int16_t)(r * 2), to565(color), nullptr);
    if (!s) return 0;
    lv_obj_t* o = s->obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, cx - r, cy - r);
    lv_obj_set_size(o, r * 2, r * 2);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_width(o, 0, 0);
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

// ============================================================
//  帧管理（LuaGameApp 调用）
// ============================================================

void luaDisplayBeginFrame(LuaDisplayContext* ctx)
{
    if (!ctx) return;
    // 清理上一帧多出的槽位（本帧绘制数减少时残留的对象）
    // 双重保护：slotAcquire 已钳制 slotIndex ≤ MaxSlots，此处再兜底防越界
    for (int i = ctx->slotIndex; i < ctx->prevSlotCount && i < LuaDisplayContext::MaxSlots; i++)
    {
        if (ctx->slots[i].obj)
        {
            lv_obj_delete(ctx->slots[i].obj);
            ctx->slots[i].obj = nullptr;
            ctx->slots[i].kind = 0;
        }
    }
    ctx->prevSlotCount = ctx->slotIndex;
    ctx->slotIndex = 0;
}
