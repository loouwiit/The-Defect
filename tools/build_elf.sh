#!/usr/bin/env bash
# 构建 ELF 插件 .so
# 用法: tools/build_elf.sh <name>   如 tools/build_elf.sh smoke
set -e
NAME="${1:?用法: build_elf.sh <name>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/elfapps/$NAME"

[ -d "$DIR" ] || { echo "无 elfapps/$NAME"; exit 1; }

cd "$DIR"
idf.py -G 'Unix Makefiles' so

echo "→ $DIR/build/$NAME.so"
