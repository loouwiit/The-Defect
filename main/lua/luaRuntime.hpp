#pragma once

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

/**
 * @brief RAII 封装 lua_State* 生命周期
 *
 * 管理 Lua 虚拟机的创建/销毁，提供脚本加载、函数调用等基本操作。
 * 每个 LuaGameApp 持有独立实例，游戏间状态完全隔离。
 */
class LuaRuntime {
public:
    LuaRuntime();
    ~LuaRuntime();

    // 禁止拷贝和移动
    LuaRuntime(const LuaRuntime&) = delete;
    LuaRuntime& operator=(const LuaRuntime&) = delete;
    LuaRuntime(LuaRuntime&&) = delete;
    LuaRuntime& operator=(LuaRuntime&&) = delete;

    /// 初始化 Lua VM：打开标准库 + 注册自定义模块
    bool init();

    /// 从 FAT 文件加载并执行 Lua 脚本（luaL_loadfile + lua_pcall）
    bool loadScript(const char* path);

    /// 从内存字符串加载并执行 Lua 脚本（luaL_loadstring + lua_pcall）
    bool loadString(const char* code);
    bool loadString(const std::string& code) { return loadString(code.c_str()); }

    /// 调用全局函数
    /// @param name  函数名
    /// @param nargs 参数个数（已压栈）
    /// @param nresults 期待的返回值个数（LUA_MULTRET 表示不限）
    /// @return true 调用成功
    bool callFunction(const char* name, int nargs = 0, int nresults = 0);

    /// 检查 Lua 调用返回值，出错时打印错误
    /// @param ret lua_pcall / lua_resume 的返回值
    /// @return true 表示无错误
    bool checkError(int ret);

    /// 检查全局函数是否存在
    bool hasFunction(const char* name);

    /// 获取裸 lua_State*（绑定函数注册用）
    lua_State* state() const { return L; }

    /// 从栈顶弹出字符串
    std::string popString();

    /// 从栈顶弹出布尔值
    bool popBool();

    /// 从栈顶弹出整数
    int popInteger();

    /// 注册 C 函数到全局表
    void registerCFunction(const char* name, lua_CFunction func);

private:
    lua_State* L{nullptr};
};
