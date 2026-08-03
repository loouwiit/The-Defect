extern "C"
{
	#include "lauxlib.h"
}
#include "luaModules.hpp"
#include <esp_log.h>
#include <esp_random.h>

static constexpr char TAG[] = "LuaModules";

// luaopen 声明
extern "C" {
    int luaopen_display(lua_State* L);
    int luaopen_input(lua_State* L);
    int luaopen_audio(lua_State* L);
    int luaopen_thread(lua_State* L);
    int luaopen_storage(lua_State* L);
}

void luaRegisterModules(lua_State* L)
{
    if (!L) {
        ESP_LOGE(TAG, "luaRegisterModules: lua_State 为空");
        return;
    }

    // 注册自定义模块
    luaL_requiref(L, "display", luaopen_display, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "input", luaopen_input, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "audio", luaopen_audio, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "thread", luaopen_thread, 1);
    lua_pop(L, 1);

    luaL_requiref(L, "storage", luaopen_storage, 1);
    lua_pop(L, 1);

    // 设置随机种子
    lua_getglobal(L, "math");
    if (lua_istable(L, -1)) {
        lua_pushinteger(L, (lua_Integer)esp_random());
        lua_setfield(L, -2, "randomseed");
    }
    lua_pop(L, 1);

    ESP_LOGI(TAG, "自定义 Lua 模块注册完成 (display/input/audio/thread/storage)");
}
