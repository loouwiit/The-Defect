#!/usr/bin/env bash
# Apply patches to managed_components
# Run from project root:  bash patches/apply.sh
# To revert:              bash patches/apply.sh --revert
#
# The patch file uses paths like "src/display/..." (stripped of the
# managed_components/espressif__esp_lvgl_adapter/ prefix).
# We cd into the component dir and use -p1 to strip the leading "src/".

set -euo pipefail

PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$PATCH_DIR/.." && pwd)"
COMPONENT_DIR="managed_components/espressif__esp_lvgl_adapter"

PATCH_OPTS="-d $PROJECT_DIR/$COMPONENT_DIR -p1 --no-backup-if-mismatch --forward -t"
IDF_PATCH_OPTS="-d $IDF_PATH/components -p3 --no-backup-if-mismatch --forward -t"

apply_patch() {
    for p in "$PATCH_DIR"/*.patch; do
        echo "Applying: $(basename "$p")"
        # 判断补丁目标：若补丁路径以 "components/" 开头则打到 ESP-IDF，否则打到 managed_components
        if head -5 "$p" | grep -q "^--- a/components/"; then
            echo "  → target: ESP-IDF components"
            if [ -z "${IDF_PATH:-}" ]; then
                echo "  ⚠ IDF_PATH not set, skipping"
                continue
            fi
            patch $IDF_PATCH_OPTS < "$p" || echo "  ⚠ patch may have already been applied"
        else
            patch $PATCH_OPTS < "$p"
        fi
    done
}

revert_patch() {
    for p in "$PATCH_DIR"/*.patch; do
        echo "Reverting: $(basename "$p")"
        if head -5 "$p" | grep -q "^--- a/components/"; then
            if [ -n "${IDF_PATH:-}" ]; then
                patch $IDF_PATCH_OPTS -R < "$p" || echo "  ⚠ already reverted"
            fi
        else
            patch $PATCH_OPTS -R < "$p" || echo "  ⚠ already reverted"
        fi
    done
}

check_patch() {
    for p in "$PATCH_DIR"/*.patch; do
        echo "Checking: $(basename "$p")"
        if head -5 "$p" | grep -q "^--- a/components/"; then
            if [ -n "${IDF_PATH:-}" ]; then
                patch $IDF_PATCH_OPTS --dry-run < "$p" 2>&1 || true
            fi
        else
            patch $PATCH_OPTS --dry-run < "$p" 2>&1 || true
        fi
    done
}

case "${1:-}" in
    --revert|-r)
        echo "↩ Reverting patches..."
        if revert_patch; then
            echo "✓ Reverted. Managed components restored to original."
        else
            echo "⚠ Patch was not applied (or already reverted)."
        fi
        ;;
    --check|-c)
        if check_patch; then
            echo "→ Patches can be applied cleanly."
        else
            echo "→ Patches already applied, or conflicts exist."
        fi
        ;;
    *)
        echo "→ Applying patches to $COMPONENT_DIR..."
        if check_patch; then
            apply_patch
            echo "✓ Patches applied successfully."
        else
            echo "⚠ Patches already applied or conflicts exist. Run with --check to inspect."
        fi
        ;;
esac
