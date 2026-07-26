#pragma once

extern "C" {
#include "lua.h"
}

/**
 * @brief 注册所有自定义 Lua 模块到指定 lua_State
 *
 * 注册的模块：display, input, audio, thread
 * 同时初始化 math.randomseed
 */
void luaRegisterModules(lua_State* L);
