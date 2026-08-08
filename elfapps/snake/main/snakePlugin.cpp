/*
 * snake 插件 — 贪吃蛇（第一个真实迁移游戏）
 *
 * 主固件 AppStack 经 ElfManager::createApp("/root/elf/snake.so", ...) 创建 SnakeRoom；
 * 玩家选择人数后 room 内部 new SnakeGame（全在 .so 内，单 create_app 入口即可）。
 * 构建：cd elfapps/snake && idf.py -G 'Unix Makefiles' so
 * 上传：curl -X PUT --data-binary @build/snake.so http://<host>/root/elf/snake.so
 */
#include "elf_sdk.h"
#include "app/app.hpp"
#include "snakeRoom/snakeRoom.hpp"

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
    __attribute__((visibility("default")));

static void* snake_create_app(void* display, const void* params)
{
    (void)params;
    return new SnakeRoom((Display*)display);
}

static void snake_destroy_app(void* app)
{
    delete (App*)app;
}

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
{
    static const elf_plugin_info_t info = {
        ELF_SDK_VERSION,
        "snake",
        "贪吃蛇",
        "经典贪吃蛇游戏，吃食物成长！",
        "F:system/desktop/snake.jpg",
        snake_create_app,
        snake_destroy_app,
    };
    return &info;
}
