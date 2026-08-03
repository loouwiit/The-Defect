#pragma once

extern "C" {
#include "lua.h"
}

/// luaopen 函数，供 luaRegisterModules 注册时使用
#ifdef __cplusplus
extern "C" {
#endif
int luaopen_storage(lua_State* L);
#ifdef __cplusplus
}
#endif
