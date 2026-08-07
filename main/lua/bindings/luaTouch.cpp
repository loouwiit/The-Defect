#include "luaTouch.hpp"

static constexpr char TAG[] = "LuaTouch";

// ============================================================
//  上下文获取（通过 Lua registry）
// ============================================================

static constexpr const char* REGISTRY_KEY = "LUA_TOUCH_CTX";

static LuaTouchContext* getCtx(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    auto* ctx = (LuaTouchContext*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

// ============================================================
//  安全槽位索引
// ============================================================

static int getSlot(lua_State* L, int index)
{
    int s = (int)luaL_optinteger(L, index, 0);
    if (s < 0 || s >= LuaTouchContext::kSlotCount) s = 0;
    return s;
}

// ============================================================
//  count() — 当前按下的触点数 (0-5)
// ============================================================

static int l_touch_count(lua_State* L)
{
    auto* ctx = getCtx(L);
    lua_pushinteger(L, ctx ? ctx->downCount : 0);
    return 1;
}

// ============================================================
//  is_down(i) — 槽位 i 当前是否按下
// ============================================================

static int l_touch_is_down(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushboolean(L, 0); return 1; }
    const int s = getSlot(L, 1);
    lua_pushboolean(L, ctx->down[s]);
    return 1;
}

// ============================================================
//  x(i) / y(i) — 槽位 i 当前坐标（屏幕坐标）
// ============================================================

static int l_touch_x(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushinteger(L, 0); return 1; }
    const int s = getSlot(L, 1);
    lua_pushinteger(L, ctx->x[s]);
    return 1;
}

static int l_touch_y(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushinteger(L, 0); return 1; }
    const int s = getSlot(L, 1);
    lua_pushinteger(L, ctx->y[s]);
    return 1;
}

// ============================================================
//  is_just_pressed(i) — 上升沿：自上一帧以来 down 从 false→true
// ============================================================

static int l_touch_is_just_pressed(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushboolean(L, 0); return 1; }
    const int s = getSlot(L, 1);
    lua_pushboolean(L, ctx->down[s] && !ctx->prevDown[s]);
    return 1;
}

// ============================================================
//  is_just_released(i) — 下降沿：自上一帧以来 down 从 true→false
// ============================================================

static int l_touch_is_just_released(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushboolean(L, 0); return 1; }
    const int s = getSlot(L, 1);
    lua_pushboolean(L, !ctx->down[s] && ctx->prevDown[s]);
    return 1;
}

// ============================================================
//  get(i) — 返回 {x=, y=, down=, just_pressed=, just_released=}
// ============================================================

static int l_touch_get(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushnil(L); return 1; }
    const int s = getSlot(L, 1);

    lua_newtable(L);
    lua_pushinteger(L, ctx->x[s]);       lua_setfield(L, -2, "x");
    lua_pushinteger(L, ctx->y[s]);       lua_setfield(L, -2, "y");
    lua_pushboolean(L, ctx->down[s]);    lua_setfield(L, -2, "down");
    lua_pushboolean(L, ctx->down[s] && !ctx->prevDown[s]);
    lua_setfield(L, -2, "just_pressed");
    lua_pushboolean(L, !ctx->down[s] && ctx->prevDown[s]);
    lua_setfield(L, -2, "just_released");
    return 1;
}

// ============================================================
//  all() — 返回所有按下触点数组 {{x=, y=}, ...}
// ============================================================

static int l_touch_all(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushnil(L); return 1; }

    lua_newtable(L);
    int idx = 1;
    for (int s = 0; s < LuaTouchContext::kSlotCount; s++)
    {
        if (!ctx->down[s]) continue;
        lua_newtable(L);
        lua_pushinteger(L, ctx->x[s]); lua_setfield(L, -2, "x");
        lua_pushinteger(L, ctx->y[s]); lua_setfield(L, -2, "y");
        lua_rawseti(L, -2, idx);
        idx++;
    }
    return 1;
}

// ============================================================
//  luaopen_touch — 注册模块
// ============================================================

extern "C" int luaopen_touch(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"count",             l_touch_count},
        {"is_down",           l_touch_is_down},
        {"x",                 l_touch_x},
        {"y",                 l_touch_y},
        {"is_just_pressed",   l_touch_is_just_pressed},
        {"is_just_released",  l_touch_is_just_released},
        {"get",               l_touch_get},
        {"all",               l_touch_all},
        {NULL, NULL}
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}
