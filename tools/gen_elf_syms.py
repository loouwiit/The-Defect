#!/usr/bin/env python3
"""生成 main/elfSdk/elf_symtab.c（主固件 ELF 符号导出表，合并替代旧 4 张表）。

用法（build/ 与 elf_symtab.c 路径已固化，无需重定向）：
    python3 tools/gen_elf_syms.py                     # 生成符号表（默认 build/The-Defect.elf → main/elfSdk/elf_symtab.c）
    python3 tools/gen_elf_syms.py --empty             # 输出空表占位（鸡生蛋重建第一步，写 main/elfSdk/elf_symtab.c）
    python3 tools/gen_elf_syms.py <elf>               # 指定 .elf，仍写默认 elf_symtab.c
    python3 tools/gen_elf_syms.py --stdout            # 内容打印到 stdout 不写文件（管道用）
    python3 tools/gen_elf_syms.py --search Task       # 搜实际 mangled 名（省略 elf 用默认）
    python3 tools/gen_elf_syms.py --search <elf> <kw> # 指定 elf 搜索

鸡生蛋重建流程（删除主固件代码后旧表引用被 GC 回收的符号会链接失败）：
    1. python3 tools/gen_elf_syms.py --empty   # 空表占位，链接可过
    2. idf.py build                            # 编译出新 .elf
    3. python3 tools/gen_elf_syms.py           # 生成真实表
    4. idf.py build                            # 再编译

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

# ── 默认路径（固化：项目根 build/The-Defect.elf → main/elfSdk/elf_symtab.c） ──
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ELF = os.path.join(ROOT, "build", "The-Defect.elf")
DEFAULT_OUT = os.path.join(ROOT, "main", "elfSdk", "elf_symtab.c")

NM = os.environ.get("RISCV_NM", "riscv32-esp-elf-nm")
CXXFILT = os.environ.get("RISCV_CXXFILT", "riscv32-esp-elf-c++filt")

# ── 白名单配置（改这里即改暴露面） ──────────────────────────

# C 函数前缀（name==symbol）
C_PREFIXES = ("lv_", "esp_log", "cJSON_", "esp_lv_adapter_", "jpeg_", "esp_jpeg_", "xTask", "vTask", "esp_timer_", "heap_caps_")
# C 函数精确名（不在前缀里但插件常用）
# 注意：含 double 版数学（sqrt/pow/...）——插件用 double 运算会引用（chineseChess sqrt）；
#  libgcc 软浮点辅助（__floatdisf/__adddf3/...）由编译器按需生成，同样需要导出。
C_EXACT = {
    "snprintf", "vsnprintf", "memcmp", "strcmp", "strlen", "memset", "memcpy", "memmove",
    "strstr", "strchr", "strrchr",
    "atan2f", "sqrtf", "ceilf", "floorf", "fabsf", "fmodf", "powf", "sinf", "cosf",
    "atan2", "sqrt", "ceil", "floor", "fabs", "fmod", "pow", "sin", "cos",
    "rand", "srand", "esp_random",
    "__assert_func", "time",
    # 文件 API（newlib/fatfs；插件读文件用——fruitNinja 读 JPG、存档等）
    "fopen", "fclose", "fread", "fwrite", "fseek", "ftell", "feof", "ferror", "fflush",
    "remove", "rename", "stat", "mkdir", "opendir", "readdir", "closedir",
}

# libgcc 软浮点/整数辅助函数前缀（编译器按需生成，.so 用 double/64 位运算会引用；
# 覆盖 __floatdisf/__floatsidf/__adddf3/__divdf3/__extendsfdf2/__truncdfsf2/__cmpdf2 等，
# 64 位除法/取模 __udivdi3/__divdi3/__umoddi3/__moddi3/__udivsi3 等，
# 以及位运算辅助 __clzsi2/__ctzsi2/__popcountsi2（__builtin_clz/ctz 生成））
LIBGCC_PREFIXES = ("__float", "__add", "__sub", "__mul", "__div", "__udiv", "__mod", "__umod",
                   "__extend", "__trunc", "__fix", "__cmp", "__neg", "__clz", "__ctz", "__popcount")

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
# 注意：__cxa_guard_abort / __cxa_pure_virtual 已被 --gc-sections 回收
# （-fno-exceptions 下 guard_abort 路径不引用；无纯虚函数调用场景），
# .elf 中不存在 → 不能放在 MUST_HAVE（否则生成报错），插件若用到需在
# elf_sdk.cpp 显式引用强制保留后再加回。
RUNTIME_EXACT = {
    "_Znwj", "_Znaj", "_ZdlPv", "_ZdaPv", "_ZdlPvj",
    "_ZSt17__throw_bad_allocv", "_ZSt20__throw_length_errorPKc",
    "_ZSt28__throw_bad_array_new_lengthv", "_ZSt19__throw_logic_errorPKc",
    "__cxa_guard_acquire", "__cxa_guard_release",
    "__cxa_atexit",
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


def write_output(content, stdout_mode):
    """写符号表内容：默认写 DEFAULT_OUT，--stdout 时打印到 stdout。"""
    if stdout_mode:
        sys.stdout.write(content)
        sys.stdout.flush()
    else:
        with open(DEFAULT_OUT, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"# wrote {DEFAULT_OUT} ({content.count('extern int')} symbols)", file=sys.stderr)


def emit_empty(reason, stdout_mode=False):
    """输出空符号表占位 + 警告（stderr）。

    用于鸡生蛋重建流程：删除主固件代码后旧符号表引用已回收符号会链接失败，
    此时先生成空表占位让编译通过，再从新 .elf 重新生成真实符号表。
    """
    print(reason, file=sys.stderr)
    print("WARNING: 输出为空符号表占位（链接可用，但插件运行时无符号可解析）；", file=sys.stderr)
    print("         主固件重新编译成功后，重跑本脚本生成真实符号表。", file=sys.stderr)
    out = [
        "/* 由 tools/gen_elf_syms.py 自动生成 — 勿手改。",
        " * ⚠ 空符号表：生成时出错（见 stderr），仅占位让链接通过。",
        " *   主固件重新编译成功后，重跑本脚本生成真实符号表。 */",
        '#include "esp_elf.h"',
        "#include <stddef.h>",
        "",
        "const struct esp_elfsym g_main_elfsyms[] = {",
        "    { NULL, NULL }",
        "};",
        "",
    ]
    write_output("\n".join(out), stdout_mode)


def main():
    stdout_mode = False
    args = sys.argv[1:]
    if "--stdout" in args:
        stdout_mode = True
        args = [a for a in args if a != "--stdout"]

    # --search 模式：python3 gen_elf_syms.py --search [<elf>] <关键字>
    if args and args[0] == "--search":
        if len(args) >= 3:
            cmd_search(args[1], args[2])
        else:
            cmd_search(DEFAULT_ELF, args[1])
        sys.exit(0)
    # --empty 模式：输出空符号表占位（鸡生蛋重建流程第一步）
    #   删除主固件代码后旧表引用被 GC 回收的符号会链接失败，先用空表占位编译，
    #   编译成功后重跑本脚本（无参数）生成真实符号表，再编译一次。
    if args and args[0] == "--empty":
        emit_empty("生成空符号表占位（--empty）", stdout_mode)
        sys.exit(0)
    elf = args[0] if args else DEFAULT_ELF

    # 1. nm 提取全局函数（T/t 全局 + W/w weak + A ROM 绝对符号如 memcmp）
    try:
        nm_out = subprocess.check_output([NM, "--defined-only", "-g", elf], text=True)
    except FileNotFoundError:
        print(f"ERROR: 找不到 {NM}，请确认 ESP-IDF 工具链在 PATH", file=sys.stderr)
        sys.exit(2)
    except subprocess.CalledProcessError:
        emit_empty(f"ERROR: nm 无法读取 {elf}（文件不存在或非 ELF？）", stdout_mode)
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
                    or dem.startswith("__cxa_") or dem.startswith("std::")):
                selected.add(name)
                continue
            continue
        if name.startswith(C_PREFIXES) or name in C_EXACT:
            selected.add(name)
            continue
        if name.startswith(LIBGCC_PREFIXES):
            selected.add(name)
            continue

    # 4. 存在性校验：MUST_HAVE 必须在 .elf 且被选中
    missing = sorted(s for s in MUST_HAVE if s not in func_names)
    if missing:
        emit_empty("ERROR: 主固件 .elf 缺少必需符号（白名单/编码可能错误，用"
                   " gen_elf_syms.py --search <elf> <关键字> 查实际名）:"
                   " " + " ".join(missing), stdout_mode)
        sys.exit(3)
    unselected = sorted(s for s in MUST_HAVE if s in func_names and s not in selected)
    if unselected:
        emit_empty("ERROR: 必需符号存在但未被白名单选中（过滤规则漏配）:"
                   " " + " ".join(unselected), stdout_mode)
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
    write_output("\n".join(out), stdout_mode)
    print(f"# {len(sel)} symbols exported (from {len(func_names)} global funcs)",
          file=sys.stderr)


if __name__ == "__main__":
    main()
