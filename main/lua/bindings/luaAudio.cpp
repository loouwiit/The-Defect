#include "audio/Audio.hpp"
extern "C"
{
#include "lua.h"
#include "lauxlib.h"
}
#include <esp_log.h>
#include <vector>

static constexpr char TAG[] = "LuaAudio";

// 本模块发起的 BGM 句柄：保留所有权以便 stop_bgm / 游戏退出时停止（防止 detach 后残留）。
// 游戏单实例运行（AppStack 栈顶唯一），文件级容器即可。
static std::vector<AudioHandle> s_bgmHandles;

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
//  play_bgm(path) — 播放背景音乐（循环，可被 stop_bgm 停止）
// ============================================================

static int l_audio_play_bgm(lua_State* L)
{
    const char* path = luaL_checkstring(L, 1);
    // ⚠ 先移动构造（右值）再 setLoop：AudioHandle 拷贝已删除，链式 = 拷贝构造失败
    auto h = Audio::play(path);
    h.setLoop(true);
    h.play();
    s_bgmHandles.push_back(std::move(h));
    return 0;
}

// ============================================================
//  stop_bgm() — 停止本模块发起的循环音乐
// ============================================================

static int l_audio_stop_bgm(lua_State* L)
{
    // AudioHandle 析构（绑定模式）会停止播放并等待解码 task 退出
    s_bgmHandles.clear();
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

// 游戏退出时停止所有 BGM（由 LuaGameApp::deinit 调用，防止循环播放残留）
void luaAudioShutdown()
{
    s_bgmHandles.clear();
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
