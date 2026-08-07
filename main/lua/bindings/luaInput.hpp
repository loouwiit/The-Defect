#pragma once

#include "gamepad/gamepadState.hpp"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * @brief Lua input 绑定的上下文
 *
 * 通过 lua_upvalue 注入到 input API 函数中。
 * LuaGameApp 在每帧游戏循环前更新这些指针。
 */
struct LuaInputContext {
    const GamepadState* states[MaxPlayers]{};      // 当前帧快照
    const GamepadState* prevStates[MaxPlayers]{};  // 上一帧快照
    bool connected[MaxPlayers]{};                  // 真实连接状态（BLE 链接是否建立）
    int playerCount{0};                             // 已连接手柄数
};

/// luaopen 函数，供 luaRegisterModules 注册时使用
#ifdef __cplusplus
extern "C" {
#endif
int luaopen_input(lua_State* L);
#ifdef __cplusplus
}
#endif
