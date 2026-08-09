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

# ── 打包插件包（/root/elf/<name>/{manifest.json, <name>.so, icon.jpg}） ──
# 元数据单一来源 = plugin_meta.json（name/display_name/description）；
# main/icon 由脚本按目录名补全；图标 = 插件工程根 icon.jpg（开发者自备，勿依赖主项目资源）
PKG="$DIR/build/pkg"
rm -rf "$PKG"
mkdir -p "$PKG"

cp "$SO" "$PKG/"

ICON_SRC="$DIR/icon.jpg"
if [ -f "$ICON_SRC" ]; then
    cp "$ICON_SRC" "$PKG/icon.jpg"
    echo "✓ 图标: icon.jpg 已打包"
else
    echo "⚠ 插件工程根目录缺 icon.jpg（桌面卡片将显示空白，请开发者自备）"
fi

META="$DIR/plugin_meta.json"
if [ -f "$META" ]; then
    python3 - "$META" "$PKG/manifest.json" "$NAME" <<'PY'
import json, sys
meta_path, out_path, dirname = sys.argv[1], sys.argv[2], sys.argv[3]
with open(meta_path, encoding='utf-8') as f:
    meta = json.load(f)
manifest = {
    "name": meta.get("name", dirname),
    "display_name": meta.get("display_name", dirname),
    "description": meta.get("description", ""),
    "main": dirname + ".so",
    "icon": "F:elf/" + dirname + "/icon.jpg",
}
with open(out_path, 'w', encoding='utf-8') as f:
    json.dump(manifest, f, ensure_ascii=False, indent=4)
print("✓ manifest.json 生成")
PY
else
    printf '{\n    "name": "%s",\n    "display_name": "%s",\n    "description": "",\n    "main": "%s.so",\n    "icon": "F:elf/%s/icon.jpg"\n}\n' \
        "$NAME" "$NAME" "$NAME" "$NAME" > "$PKG/manifest.json"
    echo "⚠ 无 plugin_meta.json，manifest 用目录名兜底"
fi

echo "→ 插件包: $PKG/  （$PKG/$NAME.so + manifest.json + icon.jpg）"
echo ""
echo "上传（把 <host> 换成设备 IP）："
echo "  curl -X PUT http://<host>/root/elf/$NAME/ -d ''"
echo "  curl -X PUT --data-binary @$PKG/$NAME.so http://<host>/root/elf/$NAME/$NAME.so"
echo "  curl -X PUT --data-binary @$PKG/manifest.json http://<host>/root/elf/$NAME/manifest.json"
[ -f "$PKG/icon.jpg" ] && \
    echo "  curl -X PUT --data-binary @$PKG/icon.jpg http://<host>/root/elf/$NAME/icon.jpg"
echo ""
