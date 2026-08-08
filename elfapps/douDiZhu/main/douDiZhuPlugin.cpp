/*
 * douDiZhu 插件 — 斗地主（联机：纯 TCP P4 远程 + AI 引擎）
 *
 * 主固件 AppStack 经 ElfManager::createApp("/root/elf/doudizhu.so", ...) 创建 DouDizhuRoom；
 * 房间内流转：单机 AI / 多主机联机 → DouDizhuApp，从机模式 → DouDizhuRemoteApp，
 * 全部在 .so 内，单 create_app 入口即可。
 * 构建：cd elfapps/douDiZhu && idf.py so   （须 Ninja 生成器，见 tools/build_elf.sh）
 * 上传：curl -X PUT --data-binary @build/douDiZhu.so http://<host>/root/elf/douDiZhu.so
 */
#include "elf_sdk.h"
#include "app/app.hpp"
#include "app/douDiZhu/douDiZhuRoom.hpp"

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
    __attribute__((visibility("default")));

static void* doudizhu_create_app(void* display, const void* params)
{
    (void)params;
    return new DouDizhuRoom((Display*)display);
}

static void doudizhu_destroy_app(void* app)
{
    delete (App*)app;
}

extern "C" const elf_plugin_info_t* elf_plugin_entry(void)
{
    static const elf_plugin_info_t info = {
        ELF_SDK_VERSION,
        "doudizhu",
        "斗地主",
        "三人斗地主，比拼牌技！支持多人联机",
        "F:system/desktop/douDiZhu.jpg",
        doudizhu_create_app,
        doudizhu_destroy_app,
    };
    return &info;
}
