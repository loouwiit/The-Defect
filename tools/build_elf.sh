#!/usr/bin/env bash
# 构建 ELF 插件 .so
# 用法: tools/build_elf.sh <name>
#
# 是否去除调试信息自动由插件 sdkconfig 决定（唯一事实来源）：
#   CONFIG_COMPILER_OPTIMIZATION_DEBUG=y（-Og）或 NONE=y（-O0）→ 保留（调试构建）
#   其余（SIZE=-Os / PERF=-O2 / 未显式设置=IDF 默认 -Og）→ strip（生产构建）
#   注：v5.5 的 LEVEL_DEBUG/LEVEL_RELEASE 是经 sdkconfig.rename 映射回同一 choice 的旧写法
#
# ⚠ 生成器用 Ninja（idf.py 默认），勿用 Unix Makefiles：
#   Makefiles 生成器对"自定义 .so 目标 → 组件库(libmain.a) 跨目录依赖"有缺陷
#   （子 make 只读 so.dir/build.make，不含子目录的 libmain.a 规则；smoke 能过仅因
#    其 libmain.a 早已存在）。Ninja 的 DAG 原生解析该依赖。
# ⚠ 若存在旧的 Unix Makefiles 构建残留（build/ 下有 Makefile/Makefile2），须先删除：
#   rm -rf elfapps/<name>/build
set -e
NAME="${1:?用法: build_elf.sh <name>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/elfapps/$NAME"

[ -d "$DIR" ] || { echo "无 elfapps/$NAME"; exit 1; }

cd "$DIR"
idf.py so

SO="$DIR/build/$NAME.so"

# 依据 sdkconfig 的优化级别 choice 决定是否 strip（行首匹配，忽略 "is not set" 注释行）
# debug（-Og/-O0）→ 保留调试信息（可 gdb/addr2line）；release（-Os/-O2）→ 去除
KEEP_DEBUG=0
if grep -qE '^CONFIG_COMPILER_OPTIMIZATION_(DEBUG|NONE)=y' "$DIR/sdkconfig"; then
    KEEP_DEBUG=1
fi

# --strip-debug 只删 .debug_* 段，.dynsym/.symtab 保留 → dlsym 与 nm 符号检查均不受影响
if [ "$KEEP_DEBUG" = "1" ]; then
    echo "✓ sdkconfig=DEBUG/NONE（-Og/-O0）：保留调试信息（可 gdb/addr2line）"
else
    riscv32-esp-elf-strip --strip-debug "$SO"
    echo "✓ sdkconfig=release（-Os/-O2）：已去除调试信息（--strip-debug）：$SO"
fi

echo "→ $SO"

# 符号检查：.so 的 UND 符号必须能由主固件 .elf 解析（否则 dlopen 失败）
# 缺失 → 非零退出，阻止上传带病插件
"$ROOT/tools/check_elf_syms.sh" "$NAME"
