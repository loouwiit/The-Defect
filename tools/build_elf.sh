#!/usr/bin/env bash
# 构建 ELF 插件 .so
# 用法: tools/build_elf.sh <name>   如 tools/build_elf.sh snake
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

echo "→ $DIR/build/$NAME.so"

# 符号检查：.so 的 UND 符号必须能由主固件 .elf 解析（否则 dlopen 失败）
# 缺失 → 非零退出，阻止上传带病插件
"$ROOT/tools/check_elf_syms.sh" "$NAME"
