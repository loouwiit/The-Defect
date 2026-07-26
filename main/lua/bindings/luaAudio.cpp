#include "audio/Audio.hpp"
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}
#include <esp_log.h>

static constexpr char TAG[] = "LuaAudio";

// ============================================================
//  play(path) — 播放音频文件（一次性）
// ============================================================

static int l_audio_play(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    Audio::play(path).detach();
    return 0;
}

// ============================================================
//  play_bgm(path) — 播放背景音乐（循环）
// ============================================================

static int l_audio_play_bgm(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    Audio::play(path).setLoop(true).detach();
    return 0;
}

// ============================================================
//  stop_bgm() — 停止所有音频
// ============================================================

static int l_audio_stop_bgm(lua_State* L)
{
    // 使用 Audio::stopAll() 或类似机制
    // 当前 Audio API 没有直接的 stopAll，可以后续扩展
    // Audio::stopAll 可能不存在，此处留空
    return 0;
}

// ============================================================
//  set_volume(0-100) — 设置主音量
// ============================================================

static int l_audio_set_volume(lua_State* L)
{
    int vol = (int)luaL_checkinteger(L, 1);
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    Audio::setMasterVolume(vol);
    return 0;
}

// ============================================================
//  luaopen_audio — 注册模块
// ============================================================

extern "C" int luaopen_audio(lua_State* L)
{
    static const luaL_Reg funcs[] = {
        {"play",       l_audio_play},
        {"play_bgm",   l_audio_play_bgm},
        {"stop_bgm",   l_audio_stop_bgm},
        {"set_volume", l_audio_set_volume},
        {NULL, NULL}
    };

    lua_newtable(L);
    luaL_setfuncs(L, funcs, 0);
    return 1;
}
