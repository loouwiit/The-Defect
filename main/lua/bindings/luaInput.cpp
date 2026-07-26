#include "luaInput.hpp"
#include <cstring>
#include <cctype>
#include <esp_log.h>

static constexpr char TAG[] = "LuaInput";

// ============================================================
//  按钮名 → GamepadButton 枚举
// ============================================================

struct BtnMap {
    const char* name;
    GamepadButton btn;
    uint8_t dpadValue;  // 15=不适用（普通按钮）
};

static const BtnMap s_btnMap[] = {
    {"a",      BTN_A,      15},
    {"b",      BTN_B,      15},
    {"x",      BTN_X,      15},
    {"y",      BTN_Y,      15},
    {"start",  BTN_START,  15},
    {"select", BTN_SELECT, 15},
    {"l1",     BTN_TL,     15},
    {"r1",     BTN_TR,     15},
    {"l2",     BTN_TL2,    15},
    {"r2",     BTN_TR2,    15},
    {"l3",     BTN_L3,     15},
    {"r3",     BTN_R3,     15},
    {"up",     (GamepadButton)0, 0},      // dpad=0
    {"down",   (GamepadButton)0, 4},      // dpad=4
    {"left",   (GamepadButton)0, 6},      // dpad=6
    {"right",  (GamepadButton)0, 2},      // dpad=2
};

static constexpr int s_btnMapCount = sizeof(s_btnMap) / sizeof(s_btnMap[0]);

/// 大小写不敏感比较
static bool streq_ci(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

/// 在 map 中查找按钮名，返回索引；-1=未找到
static int findBtn(const char* name)
{
    for (int i = 0; i < s_btnMapCount; i++) {
        if (streq_ci(name, s_btnMap[i].name)) return i;
    }
    return -1;
}

/// 检查某玩家的 dpad 是否为指定方向
static bool checkDpad(const GamepadState& state, uint8_t dpadValue)
{
    return state.dpad == dpadValue;
}

// ============================================================
//  获取安全的 player 索引
// ============================================================

static int getPlayer(lua_State* L, int index)
{
    int p = (int)luaL_optinteger(L, index, 0);
    if (p < 0 || p >= MaxPlayers) p = 0;
    return p;
}

// ============================================================
//  上下文获取（通过 Lua registry）
// ============================================================

static constexpr const char* REGISTRY_KEY = "LUA_INPUT_CTX";

static LuaInputContext* getCtx(lua_State* L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, REGISTRY_KEY);
    auto* ctx = (LuaInputContext*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return ctx;
}

// ============================================================
//  is_pressed(btn, player) — 默认 player=0
// ============================================================

static int l_input_is_pressed(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushboolean(L, 0); return 1; }

    const char* name = luaL_checkstring(L, 1);
    int player = getPlayer(L, 2);
    if (!ctx->states[player]) { lua_pushboolean(L, 0); return 1; }

    int idx = findBtn(name);
    if (idx < 0) { lua_pushboolean(L, 0); return 1; }

    const auto& state = *ctx->states[player];
    bool pressed = false;

    if (s_btnMap[idx].dpadValue <= 7) {
        // D-pad 方向
        pressed = checkDpad(state, s_btnMap[idx].dpadValue);
    } else {
        // 普通按钮
        pressed = state.isPressed(s_btnMap[idx].btn);
    }

    lua_pushboolean(L, pressed);
    return 1;
}

// ============================================================
//  is_just_pressed(btn, player) — 边沿检测
// ============================================================

static int l_input_is_just_pressed(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushboolean(L, 0); return 1; }

    const char* name = luaL_checkstring(L, 1);
    int player = getPlayer(L, 2);
    if (!ctx->states[player] || !ctx->prevStates[player]) {
        lua_pushboolean(L, 0); return 1;
    }

    int idx = findBtn(name);
    if (idx < 0) { lua_pushboolean(L, 0); return 1; }

    const auto& state = *ctx->states[player];
    const auto& prev = *ctx->prevStates[player];
    bool justPressed = false;

    if (s_btnMap[idx].dpadValue <= 7) {
        uint8_t d = s_btnMap[idx].dpadValue;
        justPressed = checkDpad(state, d) && !checkDpad(prev, d);
    } else {
        justPressed = state.isPressed(s_btnMap[idx].btn) &&
                      !prev.isPressed(s_btnMap[idx].btn);
    }

    lua_pushboolean(L, justPressed);
    return 1;
}

// ============================================================
//  joystick(id, player) — 返回 {x, y} 表
// ============================================================

static int l_input_joystick(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushnil(L); return 1; }

    const char* id = luaL_checkstring(L, 1);
    int player = getPlayer(L, 2);
    if (!ctx->states[player]) { lua_pushnil(L); return 1; }

    const auto& state = *ctx->states[player];

    lua_newtable(L);
    if (streq_ci(id, "left")) {
        lua_pushinteger(L, state.lx); lua_setfield(L, -2, "x");
        lua_pushinteger(L, state.ly); lua_setfield(L, -2, "y");
    } else if (streq_ci(id, "right")) {
        lua_pushinteger(L, state.rx); lua_setfield(L, -2, "x");
        lua_pushinteger(L, state.ry); lua_setfield(L, -2, "y");
    } else {
        lua_pushinteger(L, 128); lua_setfield(L, -2, "x");
        lua_pushinteger(L, 128); lua_setfield(L, -2, "y");
    }
    return 1;
}

// ============================================================
//  buttons(player) — 返回按钮位图
// ============================================================

static int l_input_buttons(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushinteger(L, 0); return 1; }

    int player = getPlayer(L, 1);
    if (!ctx->states[player]) { lua_pushinteger(L, 0); return 1; }

    lua_pushinteger(L, ctx->states[player]->buttons);
    return 1;
}

// ============================================================
//  connected(player) — 检查某槽位是否有手柄连接
// ============================================================

static int l_input_connected(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushboolean(L, 0); return 1; }

    int player = getPlayer(L, 1);
    if (!ctx->states[player]) { lua_pushboolean(L, 0); return 1; }

    const auto& s = *ctx->states[player];
    // 非零按钮或 D-pad 非松开状态视为有连接
    bool connected = (s.buttons != 0 || s.dpad != 15);
    lua_pushboolean(L, connected);
    return 1;
}

// ============================================================
//  player_count() — 返回已连接手柄数
// ============================================================

static int l_input_player_count(lua_State* L)
{
    auto* ctx = getCtx(L);
    if (!ctx) { lua_pushinteger(L, 0); return 1; }

    lua_pushinteger(L, ctx->playerCount);
    return 1;
}

// ============================================================
//  luaopen_input — 注册模块
// ============================================================

extern "C" int luaopen_input(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"is_pressed",      l_input_is_pressed},
        {"is_just_pressed", l_input_is_just_pressed},
        {"joystick",        l_input_joystick},
        {"buttons",         l_input_buttons},
        {"connected",       l_input_connected},
        {"player_count",    l_input_player_count},
        {NULL, NULL}
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}
