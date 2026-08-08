#!/usr/bin/env bash
# 校验主固件 ELF 中存在 ELF 符号表所需的核心符号（ABI mangled 名核对）。
# 用法:
#   tools/check_elf_syms.sh build/The-Defect.elf                  # 校验核心符号
#   tools/check_elf_syms.sh build/The-Defect.elf Task             # 搜某类/方法的实际 mangled 名
# ⚠ 必须在主固件 idf.py build 之后运行（ELF 需已生成）。
#
# 新架构：符号表 g_main_elfsyms 由 tools/gen_elf_syms.py 生成（elf_symtab.c），
# 白名单 + MUST_HAVE 已在生成期校验。本脚本独立核验：
#   - 无关键字：核验核心框架符号（App/单例/运行时/C 库）
#   - 有关键字：搜主固件相关 mangled 名（补白名单 / 核对用）
set -e

ELF="${1:?用法: check_elf_syms.sh <elf> [关键字]}"
NM="riscv32-esp-elf-nm"

if [ -n "$2" ]; then
    echo "== 搜索 \"$2\" 相关符号（用这里的结果修正 mangled 名 / 补白名单） =="
    "$NM" --defined-only "$ELF" | grep -i "$2" || true
    exit 0
fi

# ── 核心符号核验（Task/BleGamepad 新方法等用搜索模式确认实际 mangled 名） ──
SYMS=(
    # App 构造/析构/导航
    "_ZN3AppC1EP7Display"  "_ZN3AppC2EP7Display"
    "_ZN3AppD1Ev"          "_ZN3AppD2Ev"
    "_ZN3App7pushAppEPS_" "_ZN3App6popAppEv"
    "_ZN3App11replaceWithEPS_" "_ZN3App10setManagerEP15AppStackManager"
    # 单例直出
    "_ZN14VirtualGamepad8instanceEv" "_ZNK14VirtualGamepad8getStateEaa"
    "_ZN10BleGamepad8instanceEv"      "_ZNK10BleGamepad14connectedCountEv"
    "_ZN14BatteryManager10getPercentEv" "_ZN14BatteryManager12getVoltageMvEv" "_ZN14BatteryManager10isChargingEv"
    # C++ 运行时
    "_Znwj" "_Znaj" "_ZdlPv" "_ZdaPv" "_ZdlPvj"
    "_ZSt17__throw_bad_allocv" "_ZSt20__throw_length_errorPKc" "_ZSt28__throw_bad_array_new_lengthv" "_ZSt19__throw_logic_errorPKc"
    "__cxa_guard_acquire" "__cxa_guard_release" "__cxa_guard_abort" "__cxa_atexit" "__cxa_pure_virtual"
    # C 库（name==symbol）
    "lv_obj_create" "lv_obj_delete" "lv_obj_set_size" "lv_obj_align" "lv_obj_center" "lv_color_hex"
    "lv_label_create" "lv_label_set_text" "lv_screen_active"
    "esp_lv_adapter_lock" "esp_lv_adapter_unlock"
    "snprintf" "vsnprintf" "memcmp" "esp_log_write" "esp_log" "esp_log_timestamp"
    "atan2f" "sqrtf" "ceilf" "floorf" "fabsf"
)

echo "== 校验 $ELF 的符号存在性（$OK_CNT/$MISS_CNT） =="
OK=0; MISS=0
for s in "${SYMS[@]}"; do
    if "$NM" --defined-only "$ELF" | awk '{print $3}' | grep -qx "$s"; then
        echo "  [OK]   $s"
        OK=$((OK+1))
    else
        echo "  [MISS] $s"
        MISS=$((MISS+1))
    fi
done
echo "== 结果: $OK 存在 / $MISS 缺失 =="
[ "$MISS" -eq 0 ]
