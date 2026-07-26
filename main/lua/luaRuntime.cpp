#include "luaRuntime.hpp"
#include <cstdlib>
#include <esp_log.h>

static constexpr char TAG[] = "LuaRuntime";

// Lua 5.5 默认内存分配器（替代已移除的 luaL_newstate）
static void* luaAlloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
    (void)ud;
    (void)osize;
    if (nsize == 0) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, nsize);
}

LuaRuntime::LuaRuntime()
{
    L = lua_newstate(luaAlloc, NULL, 0);
    if (!L) {
        ESP_LOGE(TAG, "lua_newstate 失败");
    }
}

LuaRuntime::~LuaRuntime()
{
    if (L) {
        lua_close(L);
        L = nullptr;
    }
}

bool LuaRuntime::init()
{
    if (!L) return false;

    // 打开所有标准 Lua 库（Lua 5.5 API）
    // LUA_UTF8LIBK = 512，左移 1 得 1024，减 1 得 1023（覆盖全部 10 个库的 bit）
    luaL_openselectedlibs(L, (LUA_UTF8LIBK << 1) - 1, 0);

    return true;
}

bool LuaRuntime::loadScript(const char* path)
{
    if (!L) return false;

    int ret = luaL_loadfile(L, path);
    if (!checkError(ret)) return false;

    ret = lua_pcall(L, 0, LUA_MULTRET, 0);
    return checkError(ret);
}

bool LuaRuntime::loadString(const char* code)
{
    if (!L) return false;

    int ret = luaL_loadstring(L, code);
    if (!checkError(ret)) return false;

    ret = lua_pcall(L, 0, LUA_MULTRET, 0);
    return checkError(ret);
}

bool LuaRuntime::callFunction(const char* name, int nargs, int nresults)
{
    if (!L) return false;

    // 将函数压栈（函数在栈顶，参数在下面）
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {  // 检查栈顶是否为函数
        ESP_LOGW(TAG, "函数 %s 不存在或不是函数", name);
        lua_pop(L, 1 + nargs);  // 弹出函数 + 参数
        return false;
    }

    // 将函数移到参数下方，使栈顶依次为: func, arg1, arg2, ..., argN
    // lua_insert 将栈顶元素插入到指定索引位置
    // 栈当前: [arg1, ..., argN, func] → 我们想要: [func, arg1, ..., argN]
    if (nargs > 0) {
        lua_insert(L, -1 - nargs);
    }

    int ret = lua_pcall(L, nargs, nresults, 0);
    return checkError(ret);
}

bool LuaRuntime::checkError(int ret)
{
    if (!L) return false;

    if (ret != LUA_OK) {
        const char* err = lua_tostring(L, -1);
        if (err) {
            ESP_LOGE(TAG, "Lua 错误: %s", err);
        } else {
            ESP_LOGE(TAG, "Lua 错误 (ret=%d)", ret);
        }
        lua_pop(L, 1);  // 弹出错误信息
        return false;
    }
    return true;
}

bool LuaRuntime::hasFunction(const char* name)
{
    if (!L) return false;

    lua_getglobal(L, name);
    bool exists = lua_isfunction(L, -1);
    lua_pop(L, 1);
    return exists;
}

std::string LuaRuntime::popString()
{
    if (!L || lua_gettop(L) == 0) return {};

    const char* s = lua_tostring(L, -1);
    std::string result = s ? s : "";
    lua_pop(L, 1);
    return result;
}

bool LuaRuntime::popBool()
{
    if (!L || lua_gettop(L) == 0) return false;

    bool val = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return val;
}

int LuaRuntime::popInteger()
{
    if (!L || lua_gettop(L) == 0) return 0;

    int val = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    return val;
}

void LuaRuntime::registerCFunction(const char* name, lua_CFunction func)
{
    if (!L) return;

    lua_pushcfunction(L, func);
    lua_setglobal(L, name);
}
