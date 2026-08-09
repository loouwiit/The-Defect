/*
 * tetris 插件 — 俄罗斯方块（联机：WebSocket Web 客户端 + TCP P4 远程）
 *
 * 主固件 AppStack 经 ElfManager::createApp("/root/elf/tetris.so", ...) 创建 TetrisRoomApp；
 * 房间内流转：单机/联机 → TetrisApp（Host），远程模式 → TetrisRemoteApp（从机全屏渲染），
 * 全部在 .so 内，单 create_app 入口即可（同 snake 的 room→game 模式）。
 * 构建：cd elfapps/tetris && idf.py so   （须 Ninja 生成器，见 tools/build_elf.sh）
 * 上传：curl -X PUT --data-binary @build/tetris.so http://<host>/root/elf/tetris.so
 */
#include "elf_sdk.h"
#include "app/app.hpp"
#include "app/tetris/tetrisRoomApp.hpp"

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
    __attribute__((visibility("default")));

static void* tetris_create_app(void* display, const void* params)
{
    (void)params;
    return new TetrisRoomApp((Display*)display);
}

static void tetris_destroy_app(void* app)
{
    delete (App*)app;
}

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
{
    static const elf_plugin_info_t info = {
        ELF_SDK_VERSION,
        tetris_create_app,
        tetris_destroy_app,
    };
    return &info;
}
