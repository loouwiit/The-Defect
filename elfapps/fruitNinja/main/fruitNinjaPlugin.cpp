/*
 * fruitNinja 插件 — 水果忍者（canvas 渲染器最重的游戏）
 *
 * 主固件 AppStack 经 ElfManager::createApp("/root/elf/fruitNinja.so", ...) 创建 FruitNinjaRoom；
 * 玩家选择模式/人数后 room 内部 new FruitNinjaApp（全在 .so 内，单 create_app 入口即可）。
 * 构建：cd elfapps/fruitNinja && idf.py so（Ninja 生成器）
 * 上传：curl -X PUT --data-binary @build/fruitNinja.so http://<host>/root/elf/fruitNinja.so
 */
#include "elf_sdk.h"
#include "app/app.hpp"
#include "fruitNinjaRoom.hpp"

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
    __attribute__((visibility("default")));

static void* fruitNinja_create_app(void* display, const void* params)
{
    (void)params;
    return new FruitNinjaRoom((Display*)display);
}

static void fruitNinja_destroy_app(void* app)
{
    delete (App*)app;
}

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
{
    static const elf_plugin_info_t info = {
        ELF_SDK_VERSION,
        "fruitNinja",
        "水果忍者",
        "快速切水果，享受爽快连击！",
        "F:system/desktop/fruitNinja.jpg",
        fruitNinja_create_app,
        fruitNinja_destroy_app,
    };
    return &info;
}
