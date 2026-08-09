/*
 * chineseChess 插件 — 中国象棋（含 AI 引擎）
 *
 * 主固件 AppStack 经 ElfManager::createApp("/root/elf/chineseChess.so", ...) 创建 ChineseChessRoom；
 * 玩家选择对局模式后 room 内部 new ChineseChessApp（全在 .so 内，单 create_app 入口即可）。
 * 构建：cd elfapps/chineseChess && idf.py so（Ninja 生成器）
 * 上传：curl -X PUT --data-binary @build/chineseChess.so http://<host>/root/elf/chineseChess.so
 */
#include "elf_sdk.h"
#include "app/app.hpp"
#include "chineseChessRoom.hpp"

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
    __attribute__((visibility("default")));

static void* chineseChess_create_app(void* display, const void* params)
{
    (void)params;
    return new ChineseChessRoom((Display*)display);
}

static void chineseChess_destroy_app(void* app)
{
    delete (App*)app;
}

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
{
    static const elf_plugin_info_t info = {
        ELF_SDK_VERSION,
        chineseChess_create_app,
        chineseChess_destroy_app,
    };
    return &info;
}
