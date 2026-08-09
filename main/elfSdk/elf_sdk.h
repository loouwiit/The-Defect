#pragma once
/*
 * ELF 插件 ABI 契约（主固件与 .so 插件共享，必须保持 C 兼容）。
 *
 * 约定：
 *   - .so 只导出一个 C 入口：const elf_plugin_info_t* elf_plugin_entry(void)
 *     （须 extern "C" + default 可见性）
 *   - create_app 在 .so 内 new 一个 App 子类（继承主固件 App），返回 App*（void* 承载）
 *   - 生命周期由主固件 AppStack 全权管理；主固件在 delete 后调 ElfManager::onAppDestroyed
 *   - 插件内禁止文件级非 POD static（loader 不执行 .init_array）；用函数局部 static 或堆
 */
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 版本守卫 ─────────────────────────────────────────────
 * 主固件与插件必须编译同一份 elf_sdk.h（ELF_SDK_VERSION 一致）。
 * 任何影响布局的框架头文件（app.hpp/display.hpp/gamepadState.hpp 等）变更时 +1，
 * 旧 .so 会被 ElfManager 拒绝加载而非静默错位。
 * v4：移除 name（插件标识统一由插件包目录名 + manifest.json 的 name 字段提供，
 *     elf_plugin_info_t 只保留最小运行契约：版本 + 工厂）。
 */
#define ELF_SDK_VERSION 4

/* ── 插件信息 ─────────────────────────────────────────────
 * 由 .so 的 elf_plugin_entry() 返回。
 * 仅承载【运行契约】：插件标识与桌面展示元数据（显示名/描述/图标）统一走插件包
 * /root/elf/<name>/manifest.json（build_elf.sh 生成），此处不重复。
 */
typedef struct elf_plugin_info {
    uint32_t version;                          /* 必须 == ELF_SDK_VERSION */
    void* (*create_app)(void* display, const void* cjson_params);  /* 返回 App*（C++ 层 cast） */
    void  (*destroy_app)(void* app);           /* 可选；NULL 时主固件用 delete */
} elf_plugin_info_t;

/* ── 初始化：注册符号表（main.cpp 启动早期调用一次，须在任意 dlopen 之前） ──
 * 符号表 g_main_elfsyms 由 tools/gen_elf_syms.py 从主固件 .elf 生成（elf_symtab.c），
 * 白名单过滤 C 函数 / C++ 类方法（单例直出）/ C++ 运行时。.so include 主固件头文件
 * 直接调用（Xxx::instance().method() / Task::addTask 等原生风格）。
 */
void elf_sdk_init(void);

#ifdef __cplusplus
}
#endif
