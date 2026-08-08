#!/usr/bin/env bash
# 上传 ELF 插件 .so 到设备 FAT（HTTP PUT，复用主固件 httpPut，上限 10MB）
# 用法: tools/upload_elf.sh <name> <ip>   如 tools/upload_elf.sh smoke 192.168.1.100
set -e
NAME="${1:?用法: upload_elf.sh <name> <ip>}"
IP="${2:?用法: upload_elf.sh <name> <ip>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SO="$ROOT/elfapps/$NAME/build/$NAME.so"

[ -f "$SO" ] || { echo "缺少 $SO，先 tools/build_elf.sh $NAME"; exit 1; }

curl -X PUT --data-binary @"$SO" "http://$IP/root/elf/$NAME.so"
echo ""
echo "→ http://$IP/root/elf/$NAME.so"
