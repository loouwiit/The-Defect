#!/usr/bin/env python3
"""生成 main/elfSdk/elf_symtab.c（主固件 ELF 符号导出表，合并替代旧 4 张表）。

用法:
    python3 tools/gen_elf_syms.py build/The-Defect.elf > main/elfSdk/elf_symtab.c   # 生成符号表
    python3 tools/gen_elf_syms.py --search build/The-Defect.elf Task                  # 搜实际 mangled 名

从主固件 .elf 提取符号（nm --defined-only -g，含 WEAK 的 W/w 与 ROM 绝对符号 A），按白名单过滤：
  - C 函数（name==symbol）：前缀（lv_/esp_log_/cJSON_/esp_lv_adapter_/jpeg_/esp_jpeg_）+ 精确名
  - C++ 类方法（_Z*）：c++filt 反解后按可读类名白名单匹配（^Class::），规避手写 mangled 边界坑
  - C++ 运行时：精确 mangled 名 + demangled 前缀（operator new/delete、__cxa_）

生成 { "name", (void*)&name } 表：
  - &name 链接期取址（非硬编码），主固件重编地址自动更新
  - C++ mangled 名是合法 C 标识符，extern int 声明仅用于取址（不调用）
  - 与组件 tool/symbols.py 同思路，但用 nm 天然覆盖 WEAK（readelf 方案漏 weak）

白名单 MUST_HAVE 符号缺失/未被选中 → 脚本报错退出（防表缺关键符号致 .so 解析失败）。
"""
import subprocess
import sys
import os

NM = os.environ.get("RISCV_NM", "riscv32-esp-elf-nm")
CXXFILT = os.environ.get("RISCV_CXXFILT", "riscv32-esp-elf-c++filt")

# ── 白名单配置（改这里即改暴露面） ──────────────────────────

# C 函数前缀（name==symbol）
C_PREFIXES = ("lv_", "esp_log", "cJSON_", "esp_lv_adapter_", "jpeg_", "esp_jpeg_")
# C 函数精确名（不在前缀里但插件常用）
C_EXACT = {
    "snprintf", "vsnprintf", "memcmp", "strcmp", "strlen", "memset", "memcpy",
    "strstr", "strchr", "strrchr",
    "atan2f", "sqrtf", "ceilf", "floorf", "fabsf", "fmodf", "powf", "sinf", "cosf",
    "rand", "srand", "esp_random",
}

# C++ 类白名单（c++filt 反解后匹配 ^Class::）
CPP_CLASSES = [
    "App", "AppStack", "AppStackManager",
    "Task", "Thread",
    "GUI", "FontLoader",
    "Display", "ILI9881c",
    "Audio", "AudioHandle", "AudioSample", "ES8311",
    "VirtualGamepad", "BleGamepad", "BatteryManager",
    "VirtualIndev", "TimeService", "Touch", "GT911Indev",
    "Mutex", "Lock",
]

# C++ 运行时符号（精确 mangled 名；32 位 size_t → j 编码）
RUNTIME_EXACT = {
    "_Znwj", "_Znaj", "_ZdlPv", "_ZdaPv", "_ZdlPvj",
    "_ZSt17__throw_bad_allocv", "_ZSt20__throw_length_errorPKc",
    "_ZSt28__throw_bad_array_new_lengthv", "_ZSt19__throw_logic_errorPKc",
    "__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort",
    "__cxa_atexit", "__cxa_pure_virtual",
}

# 必须存在且被选中的符号（生成后校验，缺失 → 报错退出）
MUST_HAVE = RUNTIME_EXACT | {
    "lv_obj_create", "lv_label_create", "snprintf", "esp_log_write",
    "lv_color_hex", "lv_display_get_horizontal_resolution",
    "_ZN3AppC1EP7Display", "_ZN14BatteryManager10getPercentEv",
    "_ZNK14VirtualGamepad8getStateEaa",
}

# 明确排除（主固件内部/自身）
EXCLUDE = {"elf_find_sym", "g_main_elfsyms", "elf_sdk_init", "app_main"}


def cmd_search(elf, keyword):
    """搜索主固件 .elf 中与关键字匹配的全局符号（用于核对白名单/修正编码）。"""
    try:
        nm_out = subprocess.check_output([NM, "--defined-only", "-g", elf], text=True)
    except FileNotFoundError:
        print(f"ERROR: 找不到 {NM}", file=sys.stderr)
        sys.exit(2)
    print(f"== 搜索 \"{keyword}\" 相关符号（用结果修正白名单/编码） ==")
    kw = keyword.lower()
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        typ, name = parts[1], parts[2]
        if kw in name.lower():
            print(f"  [{typ}] {name}")


def main():
    # --search 模式：python3 gen_elf_syms.py --search <elf> <关键字>
    if len(sys.argv) >= 4 and sys.argv[1] == "--search":
        cmd_search(sys.argv[2], sys.argv[3])
        sys.exit(0)
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    elf = sys.argv[1]

    # 1. nm 提取全局函数（T/t 全局 + W/w weak + A ROM 绝对符号如 memcmp）
    try:
        nm_out = subprocess.check_output([NM, "--defined-only", "-g", elf], text=True)
    except FileNotFoundError:
        print(f"ERROR: 找不到 {NM}，请确认 ESP-IDF 工具链在 PATH", file=sys.stderr)
        sys.exit(2)

    func_names = []
    for line in nm_out.splitlines():
        parts = line.split()
        if len(parts) < 3:
            continue
        typ = parts[1]
        # A = 绝对符号（ROM 函数，如 memcmp/strcmp，由 ROM 链接脚本赋值到固定地址）
        if typ not in ("T", "t", "W", "w", "A"):
            continue
        name = parts[2]
        if name in EXCLUDE:
            continue
        func_names.append(name)

    # 2. c++filt 批量反解 _Z* 符号
    cxx_names = [n for n in func_names if n.startswith("_Z")]
    demapped = {}
    if cxx_names:
        try:
            filt = subprocess.run([CXXFILT], input="\n".join(cxx_names),
                                  capture_output=True, text=True, check=False)
            lines = filt.stdout.splitlines()
            for name, dem in zip(cxx_names, lines):
                demapped[name] = dem
        except FileNotFoundError:
            print(f"ERROR: 找不到 {CXXFILT}", file=sys.stderr)
            sys.exit(2)

    # 3. 白名单过滤
    selected = set()
    for name in func_names:
        if name in RUNTIME_EXACT:
            selected.add(name)
            continue
        if name.startswith("_Z"):
            dem = demapped.get(name, "")
            if any(dem.startswith(c + "::") for c in CPP_CLASSES):
                selected.add(name)
                continue
            if (dem.startswith("operator new") or dem.startswith("operator delete")
                    or dem.startswith("__cxa_")):
                selected.add(name)
                continue
            continue
        if name.startswith(C_PREFIXES) or name in C_EXACT:
            selected.add(name)
            continue

    # 4. 存在性校验：MUST_HAVE 必须在 .elf 且被选中
    missing = sorted(s for s in MUST_HAVE if s not in func_names)
    if missing:
        print("ERROR: 主固件 .elf 缺少必需符号（白名单/编码可能错误，用"
              " gen_elf_syms.py --search <elf> <关键字> 查实际名）:", file=sys.stderr)
        for s in missing:
            print(f"  {s}", file=sys.stderr)
        sys.exit(3)
    unselected = sorted(s for s in MUST_HAVE if s in func_names and s not in selected)
    if unselected:
        print("ERROR: 必需符号存在但未被白名单选中（过滤规则漏配）:", file=sys.stderr)
        for s in unselected:
            print(f"  {s}", file=sys.stderr)
        sys.exit(4)

    # 5. 生成 elf_symtab.c
    sel = sorted(selected)
    out = []
    out.append("/* 由 tools/gen_elf_syms.py 自动生成 — 勿手改。")
    out.append(" * 符号从主固件 .elf 提取，&name 链接期取址；C++ mangled 名声明为 extern int 仅用于取址。 */")
    out.append('#include "esp_elf.h"')
    out.append("#include <stddef.h>")
    out.append("")
    out.append("/* extern 声明（跳过内置声明类型不匹配告警） */")
    out.append("#pragma GCC diagnostic push")
    out.append('#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"')
    for s in sel:
        out.append(f"extern int {s};")
    out.append("#pragma GCC diagnostic pop")
    out.append("")
    out.append("const struct esp_elfsym g_main_elfsyms[] = {")
    for s in sel:
        out.append(f'    {{ "{s}", (void*)&{s} }},')
    out.append("    { NULL, NULL }")
    out.append("};")
    out.append("")
    print("\n".join(out))
    print(f"# {len(sel)} symbols exported (from {len(func_names)} global funcs)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
