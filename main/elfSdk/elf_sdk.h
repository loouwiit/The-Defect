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
 */
#define ELF_SDK_VERSION 1

/* ── 插件信息 ─────────────────────────────────────────────
 * 由 .so 的 elf_plugin_entry() 返回。
 */
typedef struct elf_plugin_info {
    uint32_t version;                          /* 必须 == ELF_SDK_VERSION */
    const char* name;                          /* 插件名（如 "smoke"） */
    void* (*create_app)(void* display, const void* cjson_params);  /* 返回 App*（C++ 层 cast） */
    void  (*destroy_app)(void* app);           /* 可选；NULL 时主固件用 delete */
} elf_plugin_info_t;

/* ── SDK 服务（主固件导出，C 名稳定，.so 直接调用） ────────
 * 这类函数在 elf_sdk.cpp 以 extern "C" 实现并在 elf_sdk_init() 注册，
 * 供 .so 通过符号解析调用；规避 C++ mangling 的脆弱性。
 * 单例类方法（VirtualGamepad::instance 等）另行在 elf_framework_syms.cpp 直接注册。
 */

/* 初始化：注册全部符号表（main.cpp 启动早期调用一次，须在任意 dlopen 之前） */
void elf_sdk_init(void);

/* Task::addTask 的 C 镜像：fn 返回下次唤醒延迟 ms；~0 表示不再唤醒 */
typedef unsigned int (*elf_task_fn_t)(void* param);
void* elf_task_add(elf_task_fn_t fn, const char* name, void* param,
                   unsigned int tick_ms, int affinity);
void  elf_task_remove(void* task);

/* BatteryManager 直读 */
int  elf_battery_percent(void);
int  elf_battery_voltage_mv(void);
int  elf_battery_charging(void);

/* BleGamepad 只读 */
unsigned int elf_gamepad_connected_count(void);
int          elf_gamepad_device_connected(unsigned int player);
unsigned int elf_gamepad_battery(unsigned int player);

#ifdef __cplusplus
}
#endif
