/*
 * elf_sdk.cpp — ELF 插件 SDK：符号表注册。
 *
 * elf_sdk_init() 在 main.cpp 启动早期调用，注册一张由脚本生成的符号表
 * （g_main_elfsyms，见 elf_symtab.c）：tools/gen_elf_syms.py 从主固件 .elf 提取
 * 符号（nm 含 WEAK），按白名单过滤 C 函数 / C++ 类方法（单例直出）/ C++ 运行时，
 * 生成 { "name", (void*)&name } 表（&name 链接期取址）。
 * .so include 主固件头文件直接调用（Xxx::instance().method() / Task::addTask 等原生风格）。
 */
#include "elf_sdk.h"
#include "esp_elf.h"

extern const struct esp_elfsym g_main_elfsyms[];

/* ── 注册（在任意 dlopen 之前调用一次；esp_elf_register_symbol 非线程安全） ── */
extern "C" void elf_sdk_init(void)
{
    esp_elf_register_symbol((esp_elf_symbol_table_t*)g_main_elfsyms);
}
