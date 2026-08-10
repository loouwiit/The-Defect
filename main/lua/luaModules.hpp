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

/** @brief 停止 Lua 音频模块发起的循环音乐（LuaGameApp::deinit 调用，防退出后 BGM 残留） */
void luaAudioShutdown();
