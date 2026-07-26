#pragma once

#include "lvgl.h"

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
    lv_obj_t* canvas{nullptr};   // 游戏渲染容器（每帧清空重建子对象）
    lv_obj_t* screen{nullptr};   // 游戏全屏 screen 对象
};

/// luaopen 函数，供 luaRegisterModules 注册时使用
#ifdef __cplusplus
extern "C" {
#endif
int luaopen_display(lua_State* L);
#ifdef __cplusplus
}
#endif
