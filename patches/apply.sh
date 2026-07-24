#!/usr/bin/env bash
# Apply patches to managed_components
# Run from project root:  bash patches/apply.sh
# To revert:              bash patches/apply.sh --revert
#
# Patch filename convention: NNNN-<component_name>-<description>.patch
#   - For managed components: component name is the suffix after "espressif__"
#     Example: esp_lvgl_adapter → managed_components/espressif__esp_lvgl_adapter/
#   - For ESP-IDF (paths starting with "components/"): targets $IDF_PATH/components/
#   - The patch paths are relative to the component root, -p1 strips "a/" prefix.

set -euo pipefail

PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$PATCH_DIR/.." && pwd)"

IDF_PATCH_OPTS="-d $IDF_PATH/components -p3 --no-backup-if-mismatch --forward -t"
COMMON_PATCH_OPTS="--no-backup-if-mismatch --forward -t"

# 从补丁文件名提取组件名（NNNN-<name>-<rest>.patch → <name>）
_patch_component() {
    local basename
    basename="$(basename "$1" .patch)"
    # 去掉 "NNNN-" 前缀
    basename="${basename#*-}"
    # 提取组件名（第一个 "-" 之前的部分）
    echo "${basename%%-*}"
}

# 判断补丁目标并执行 patch
_patch_apply() {
    local p="$1" action="$2"
    local basename component target_dir patch_opts

    basename="$(basename "$p")"
    component="$(_patch_component "$p")"

    if head -5 "$p" | grep -q "^--- a/components/"; then
        # ESP-IDF patches
        if [ -z "${IDF_PATH:-}" ]; then
            echo "  ⚠ IDF_PATH not set, skipping $basename"
            return
        fi
        echo "  → target: ESP-IDF components"
        target_dir="$IDF_PATH/components"
        patch_opts="-d $target_dir -p3 $COMMON_PATCH_OPTS"
    else
        # managed component patches
        target_dir="$PROJECT_DIR/managed_components/espressif__${component}/"
        if [ ! -d "$target_dir" ]; then
            echo "  ⚠ component dir not found: $target_dir"
            return
        fi
        echo "  → target: $target_dir"
        patch_opts="-d $target_dir -p1 $COMMON_PATCH_OPTS"
    fi

    case "$action" in
        apply)  patch $patch_opts < "$p" || echo "  ⚠ may have already been applied" ;;
        revert) patch $patch_opts -R < "$p" || echo "  ⚠ already reverted" ;;
        check)  patch $patch_opts --dry-run < "$p" 2>&1 || true ;;
    esac
}

apply_patch() {
    for p in "$PATCH_DIR"/*.patch; do
        echo "Applying: $(basename "$p")"
        _patch_apply "$p" "apply"
    done
}

revert_patch() {
    for p in "$PATCH_DIR"/*.patch; do
        echo "Reverting: $(basename "$p")"
        _patch_apply "$p" "revert"
    done
}

check_patch() {
    for p in "$PATCH_DIR"/*.patch; do
        echo "Checking: $(basename "$p")"
        _patch_apply "$p" "check"
    done
}

case "${1:-}" in
    --revert|-r)
        echo "↩ Reverting patches..."
        if revert_patch; then
            echo "✓ Reverted. Managed components restored to original."
        else
            echo "⚠ Some patches may not have been reverted."
        fi
        ;;
    --check|-c)
        echo "→ Checking patch status..."
        check_patch
        ;;
    *)
        echo "→ Applying patches..."
        if check_patch 2>/dev/null; then
            apply_patch
            echo "✓ Patches applied successfully."
        else
            echo "⚠ Patches may already be applied. Run with --check to inspect."
        fi
        ;;
esac
