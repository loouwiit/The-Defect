extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}
#include <esp_timer.h>
#include <esp_random.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr char TAG[] = "LuaThread";

// registry 键：本帧累计 sleep 时长（ms）。由 LuaGameApp 每帧 loop(dt) 前清零。
static constexpr const char* SLEEP_BUDGET_KEY = "LUA_THREAD_SLEEP_BUDGET";

// ============================================================
//  sleep(ms) — 延时
// ============================================================

static int l_thread_sleep(lua_State* L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    // ⚠ 上限保护：sleep 在 LVGL 锁内执行（gameLoop 持锁调 loop(dt)），
    //   无限/超长 sleep 会冻结 UI。钳制到 500ms。
    if (ms > 500) ms = 500;

    // ⚠ 防死循环绕过指令看门狗：sleep 是 C 函数，不消耗 Lua 指令计数——
    //   `while true do thread.sleep(10) end` 每轮仅 ~5 条 Lua 指令 → 永不触达
    //   指令预算 → loop(dt) 永不返回 → 游戏循环任务永久持 LVGL 锁 → 整个 UI 冻结。
    //   在 registry 累计本帧 sleep 时长，每帧预算 500ms，超限 luaL_error
    //   （走 LuaGameApp::reportLuaError 错误路径退出游戏）。
    lua_getfield(L, LUA_REGISTRYINDEX, SLEEP_BUDGET_KEY);
    lua_Integer budget = lua_tointeger(L, -1);
    lua_pop(L, 1);
    budget += ms;
    if (budget > 500) {
        lua_pushinteger(L, (lua_Integer)0);
        lua_setfield(L, LUA_REGISTRYINDEX, SLEEP_BUDGET_KEY);
        return luaL_error(L, "sleep budget exceeded (500ms/frame): possible infinite sleep loop");
    }
    lua_pushinteger(L, budget);
    lua_setfield(L, LUA_REGISTRYINDEX, SLEEP_BUDGET_KEY);

    if (ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    return 0;
}

// ============================================================
//  now() — 返回当前时间戳（毫秒）
// ============================================================

static int l_thread_now(lua_State* L)
{
    lua_pushinteger(L, (lua_Integer)(esp_timer_get_time() / 1000));
    return 1;
}

// ============================================================
//  random(min, max) — 范围随机整数
// ============================================================

static int l_thread_random(lua_State* L)
{
    // ⚠ 防溢出除零：int 算术下 max-min+1 在区间超过 INT_MAX 时回绕为 0
    //   → esp_random() % 0 → RISC-V 除零异常 → panic 重启（恶意/错误脚本一行即崩）。
    //   用 int64 计算区间并钳制到 esp_random() 的 32 位模数范围。
    int64_t min = luaL_checkinteger(L, 1);
    int64_t max = luaL_checkinteger(L, 2);
    if (min > max) {
        int64_t tmp = min; min = max; max = tmp;
    }
    int64_t range = max - min + 1;
    if (range <= 0) range = 1;                         // 恰好 2^32 的区间 → 钳制到 2^32-1
    if (range > 0xFFFFFFFFLL) range = 0xFFFFFFFFLL;
    lua_pushinteger(L, min + (int64_t)(esp_random() % (uint64_t)range));
    return 1;
}

// ============================================================
//  luaopen_thread — 注册模块
// ============================================================

extern "C" int luaopen_thread(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"sleep",  l_thread_sleep},
        {"now",    l_thread_now},
        {"random", l_thread_random},
        {NULL, NULL}
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}
