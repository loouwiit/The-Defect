#!/usr/bin/env bash
# 校验主固件 ELF 中是否存在 ELF 符号表所需的符号（ABI mangled 名核对）。
# 用法:
#   tools/check_elf_syms.sh build/The-Defect.elf              # 校验全部已登记符号
#   tools/check_elf_syms.sh build/The-Defect.elf VirtualGamepad  # 搜某类/方法的实际 mangled 名
# ⚠ 必须在主固件 idf.py build 之后运行（ELF 需已生成）。
set -e

ELF="${1:?用法: check_elf_syms.sh <elf> [关键字]}"
NM="riscv32-esp-elf-nm"

if [ -n "$2" ]; then
    echo "== 搜索 \"$2\" 相关符号（用这里的结果修正 mangled 名） =="
    "$NM" --defined-only "$ELF" | grep -i "$2" || true
    exit 0
fi

# ── 需校验的符号（与 elf_framework_syms.cpp / elf_cpp_runtime.cpp / elf_export_syms.c 一致） ──
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
    # C 库（smoke 需要，name==symbol）
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
