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

// ============================================================
//  sleep(ms) — 延时
// ============================================================

static int l_thread_sleep(lua_State* L)
{
    int ms = (int)luaL_checkinteger(L, 1);
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
    int min = (int)luaL_checkinteger(L, 1);
    int max = (int)luaL_checkinteger(L, 2);
    if (min > max) {
        int tmp = min; min = max; max = tmp;
    }
    lua_pushinteger(L, min + (int)(esp_random() % (uint32_t)(max - min + 1)));
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
