#!/usr/bin/env python3
"""生成 main/elfSdk/elf_export_syms.c（C 符号导出表）。

用法:
    python3 tools/gen_elf_syms.py build/The-Defect.elf > main/elfSdk/elf_export_syms.c

从主固件 .elf 提取全局 C 函数符号（lv_*/esp_log_*/math/cJSON 等），
生成 ESP_ELFSYM_EXPORT 表。R1 可用手工最小表；LVGL 升级 / 插件新增依赖后重生成。
C++ mangled 符号（_Z*）不在此表（由 elf_framework_syms.cpp / elf_cpp_runtime.cpp 处理）。
"""
import subprocess
import sys

NM = "riscv32-esp-elf-nm"

# 需要导出的前缀（C 函数 name==symbol）
PREFIXES = ("lv_", "esp_log_", "cJSON_", "esp_lv_adapter_", "jpeg_", "esp_jpeg_")
# 精确名（不在前缀里但插件常用）
EXACT = {
    "snprintf", "vsnprintf",
    "atan2f", "sqrtf", "ceilf", "floorf", "fabsf",
    "rand", "srand", "esp_random",
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    elf = sys.argv[1]

    out = subprocess.check_output([NM, "--defined-only", "-g", elf], text=True)
    syms = set()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        _addr, typ, name = parts[0], parts[1], parts[2]
        if typ not in ("T", "t", "W", "w"):      # 仅函数（含弱符号）
            continue
        if name.startswith("_"):                 # 跳过 C++ mangled / 汇编内部
            continue
        if name.startswith(PREFIXES) or name in EXACT:
            syms.add(name)

    print("/* 由 tools/gen_elf_syms.py 自动生成 — 勿手改 */")
    print('#include "esp_elf.h"')
    print()
    print("const struct esp_elfsym g_elf_export_syms[] = {")
    for s in sorted(syms):
        print(f"    ESP_ELFSYM_EXPORT({s}),")
    print("    ESP_ELFSYM_END")
    print("};")


if __name__ == "__main__":
    main()
