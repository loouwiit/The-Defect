#!/usr/bin/env bash
# 检查插件 .so 的未定义符号是否都能由主固件 .elf 解析
# 用法: tools/check_elf_syms.sh <name>   （build_elf.sh 自动调用）
#
# 原理：.so 用 -nostdlib 链接，所有外部符号保持 UND，运行时绑定到主固件符号表。
#       若某符号主固件 .elf 中没有（被 --gc-sections 回收 或 白名单漏选），
#       dlopen 会报 "Can't find common <sym>" → 无法运行。
#       本脚本在构建后立即对照，一次抓全缺失，不用运行时逐个炸。
#
# 修复指引（报缺失时）：
#   1. 主固件 .elf 里有但白名单漏选（如 sqrt/fseek/__clzsi2）
#      → tools/gen_elf_syms.py 白名单补名（C_EXACT/LIBGCC_PREFIXES），重生成符号表
#   2. 主固件 .elf 里没有（被 --gc-sections 回收，插件常用但主固件不引用）
#      → main/CMakeLists.txt 的 target_link_options INTERFACE 加一条
#        "-Wl,--undefined=<sym>"（链接器原生强制，勿用 PRIVATE——静态库不参与链接）
set -e
NAME="${1:?用法: check_elf_syms.sh <name>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SO="$ROOT/elfapps/$NAME/build/$NAME.so"
ELF="$ROOT/build/The-Defect.elf"

[ -f "$SO" ] || { echo "无插件 $SO（先 tools/build_elf.sh $NAME）"; exit 1; }
[ -f "$ELF" ] || { echo "无主固件 $ELF（先编译主固件）"; exit 1; }

# .so 未定义符号（含弱符号 w/v —— 弱 UND 若最终未解析同样会 dlopen 失败）
# nm -u 输出形如 "U symbol" / "w symbol" / "v symbol"；过滤掉本地非外部引用
und=$(riscv32-esp-elf-nm -u "$SO" | awk 'NF>=2 {print $NF}' | sort -u)

# 主固件全部已定义全局符号
def=$(riscv32-esp-elf-nm --defined-only -g "$ELF" | awk '{print $3}' | sort -u)

missing=$(comm -23 <(printf '%s\n' "$und") <(printf '%s\n' "$def"))
# comm 输入须排序去重（已做）；空输入时 comm 会输出空，printf 空串会多一行，处理一下
if [ -z "$missing" ]; then
    echo "✓ $NAME.so 所有未定义符号均能由主固件解析"
    exit 0
fi

echo "⚠ $NAME.so 以下未定义符号在主固件 .elf 中不存在（dlopen 将失败）："
echo "$missing" | grep -v '^$' | sed 's/^/    /'
echo
echo "修复："
echo "  1. 主固件 .elf 有但白名单漏选 → tools/gen_elf_syms.py 白名单补名（C_EXACT/LIBGCC_PREFIXES），重生成符号表"
echo "  2. 主固件 .elf 没有（GC 回收）→ main/CMakeLists.txt INTERFACE 加 \"-Wl,--undefined=<sym>\""
echo "  提示：riscv32-esp-elf-nm --defined-only -g build/The-Defect.elf | grep <符号> 可查是否在 .elf"
exit 1
