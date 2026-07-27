#pragma once

#include "lvgl.h"
#include "display/display.hpp"

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

/**
 * @brief Lua display 绑定的上下文
 *
 * 通过 lua_upvalue 注入到每个 display API 函数中。
 */
struct LuaDisplayContext {
    lv_obj_t* screen{};
    Display* display{};
};

/// luaopen 函数，供 luaRegisterModules 注册时使用
#ifdef __cplusplus
extern "C" {
#endif
int luaopen_display(lua_State* L);
#ifdef __cplusplus
}
#endif
