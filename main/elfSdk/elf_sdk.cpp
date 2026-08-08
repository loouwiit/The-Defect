/*
 * elf_sdk.cpp — ELF 插件 SDK：符号表注册 + 稳定 C 包装函数。
 *
 * elf_sdk_init() 在 main.cpp 启动早期调用，注册四张符号表：
 *   1. g_elf_sdk_syms      — 本文件的 extern "C" 包装（C 名稳定，name==symbol）
 *   2. g_elf_export_syms   — 生成的 C 库符号表（lv_* / esp_log_* / math…，elf_export_syms.c）
 *   3. g_elf_cpp_runtime_syms — C++ 运行时（elf_cpp_runtime.cpp）
 *   4. g_elf_framework_syms   — C++ 框架/单例（elf_framework_syms.cpp）
 *
 * .so 通过符号解析调用这些函数（如同调用主固件普通 C 函数）。
 */
#include "elf_sdk.h"
#include "esp_elf.h"
#include "task/task.hpp"
#include "battery/batteryManager.hpp"
#include "gamepad/bleGamepad.hpp"

extern const struct esp_elfsym g_elf_export_syms[];
extern const struct esp_elfsym g_elf_cpp_runtime_syms[];
extern const struct esp_elfsym g_elf_framework_syms[];
extern const struct esp_elfsym g_elf_sdk_syms[];

/* ── 注册（在任意 dlopen 之前调用一次；esp_elf_register_symbol 非线程安全） ── */
extern "C" void elf_sdk_init(void)
{
    esp_elf_register_symbol((esp_elf_symbol_table_t*)g_elf_sdk_syms);
    esp_elf_register_symbol((esp_elf_symbol_table_t*)g_elf_export_syms);
    esp_elf_register_symbol((esp_elf_symbol_table_t*)g_elf_cpp_runtime_syms);
    esp_elf_register_symbol((esp_elf_symbol_table_t*)g_elf_framework_syms);
}

/* ── SDK C 包装 ─────────────────────────────────────────── */

extern "C" void* elf_task_add(elf_task_fn_t fn, const char* name, void* param,
                              unsigned int tick_ms, int affinity)
{
    Task::Affinity af;
    if (affinity == -1)
        af = Task::Affinity(Task::Affinity::None);
    else if (affinity == -2)
        af = Task::Affinity(Task::Affinity::NotAssigned);
    else
        af = Task::Affinity((size_t)affinity);
    return Task::addTask((Task::Function_t)fn, name, param, tick_ms, af);
}

extern "C" void elf_task_remove(void* task)
{
    if (task)
        Task::removeTask((Task*)task);
}

extern "C" int elf_battery_percent(void)
{
    return BatteryManager::instance().getPercent();
}

extern "C" int elf_battery_voltage_mv(void)
{
    return BatteryManager::instance().getVoltageMv();
}

extern "C" int elf_battery_charging(void)
{
    return BatteryManager::instance().isCharging() ? 1 : 0;
}

extern "C" unsigned int elf_gamepad_connected_count(void)
{
    return BleGamepad::instance().connectedCount();
}

extern "C" int elf_gamepad_device_connected(unsigned int player)
{
    const auto* dev = BleGamepad::instance().getDevice(player);
    return (dev && dev->connected) ? 1 : 0;
}

extern "C" unsigned int elf_gamepad_battery(unsigned int player)
{
    return BleGamepad::instance().getBatteryLevel(player);
}

/* ── 本文件包装函数的符号表（C 名，name==symbol） ────────── */

const struct esp_elfsym g_elf_sdk_syms[] = {
    ESP_ELFSYM_EXPORT(elf_sdk_init),
    ESP_ELFSYM_EXPORT(elf_task_add),
    ESP_ELFSYM_EXPORT(elf_task_remove),
    ESP_ELFSYM_EXPORT(elf_battery_percent),
    ESP_ELFSYM_EXPORT(elf_battery_voltage_mv),
    ESP_ELFSYM_EXPORT(elf_battery_charging),
    ESP_ELFSYM_EXPORT(elf_gamepad_connected_count),
    ESP_ELFSYM_EXPORT(elf_gamepad_device_connected),
    ESP_ELFSYM_EXPORT(elf_gamepad_battery),
    ESP_ELFSYM_END
};
