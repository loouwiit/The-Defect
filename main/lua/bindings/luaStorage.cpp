#include "luaStorage.hpp"

extern "C" {
#include "lauxlib.h"
}

#include "storage/fat.hpp"
#include <cstdio>
#include <cstring>
#include <esp_log.h>

static constexpr char TAG[] = "LuaStorage";

// 存档根目录（沙箱白名单：脚本只能在此目录读写，防止越权访问系统文件）
static constexpr char DATA_DIR[] = "/root/lua/data/";

// 单文件大小上限（防恶意大文件吃内存）
static constexpr long MAX_FILE_SIZE = 256 * 1024;
// 文件名长度上限
static constexpr size_t MAX_NAME_LEN = 64;

/// 校验存档名合法性，拒绝路径穿越
/// @return 合法返回 true；非法返回 false
static bool sanitizeName(const char* name, char* out, size_t outSize)
{
    if (!name || !out || outSize == 0) return false;

    size_t len = strlen(name);
    if (len == 0 || len >= outSize || len > MAX_NAME_LEN) return false;

    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        // 只允许字母/数字/下划线/点/连字符，杜绝路径分隔符与 ".." 穿越
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
               || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        if (!ok) return false;
    }
    // 禁止以点开头（隐藏文件 / 相对路径）
    if (name[0] == '.') return false;

    strncpy(out, name, outSize);
    out[outSize - 1] = '\0';
    return true;
}

// storage.save(name, text) → bool
static int luaStorage_save(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);
    size_t len = 0;
    const char* text = luaL_checklstring(L, 2, &len);

    char safeName[96];
    if (!sanitizeName(name, safeName, sizeof(safeName))) {
        lua_pushboolean(L, 0);
        return 1;
    }

    // 确保存档目录存在
    newFloor("/root/lua/data");

    char path[160];
    snprintf(path, sizeof(path), "%s%s", DATA_DIR, safeName);

    FILE* f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "无法写入存档: %s", path);
        lua_pushboolean(L, 0);
        return 1;
    }
    bool ok = (fwrite(text, 1, len, f) == len);
    fclose(f);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// storage.load(name) → string | nil（不存在或失败返回 nil）
static int luaStorage_load(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);

    char safeName[96];
    if (!sanitizeName(name, safeName, sizeof(safeName))) {
        lua_pushnil(L);
        return 1;
    }

    char path[160];
    snprintf(path, sizeof(path), "%s%s", DATA_DIR, safeName);

    FILE* f = fopen(path, "r");
    if (!f) {
        lua_pushnil(L);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        lua_pushstring(L, "");
        return 1;
    }
    if (size > MAX_FILE_SIZE) {
        ESP_LOGW(TAG, "存档过大 (%ld B)，拒绝读取", size);
        fclose(f);
        lua_pushnil(L);
        return 1;
    }

    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        lua_pushnil(L);
        return 1;
    }
    size_t rd = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[rd] = '\0';
    lua_pushlstring(L, buf, rd);
    free(buf);
    return 1;
}

// storage.remove(name) → bool
static int luaStorage_remove(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);

    char safeName[96];
    if (!sanitizeName(name, safeName, sizeof(safeName))) {
        lua_pushboolean(L, 0);
        return 1;
    }

    char path[160];
    snprintf(path, sizeof(path), "%s%s", DATA_DIR, safeName);

    bool ok = removeFile(path);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// storage.exists(name) → bool
static int luaStorage_exists(lua_State* L)
{
    const char* name = luaL_checkstring(L, 1);

    char safeName[96];
    if (!sanitizeName(name, safeName, sizeof(safeName))) {
        lua_pushboolean(L, 0);
        return 1;
    }

    char path[160];
    snprintf(path, sizeof(path), "%s%s", DATA_DIR, safeName);

    FILE* f = fopen(path, "r");
    bool exists = (f != nullptr);
    if (f) fclose(f);
    lua_pushboolean(L, exists ? 1 : 0);
    return 1;
}

static const luaL_Reg storage_funcs[] = {
    { "save",   luaStorage_save },
    { "load",   luaStorage_load },
    { "remove", luaStorage_remove },
    { "exists", luaStorage_exists },
    { nullptr, nullptr }
};

extern "C" int luaopen_storage(lua_State* L)
{
    luaL_newlib(L, storage_funcs);
    return 1;
}
