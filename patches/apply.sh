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
PATCH_FILE="$PATCH_DIR/0001-esp_lvgl_adapter-frame-ready-callback.patch"
COMPONENT_DIR="managed_components/espressif__esp_lvgl_adapter"

PATCH_OPTS="-d $PROJECT_DIR/$COMPONENT_DIR -p1 --no-backup-if-mismatch --forward -t"

apply_patch() {
    patch $PATCH_OPTS < "$PATCH_FILE"
}

revert_patch() {
    patch $PATCH_OPTS -R < "$PATCH_FILE"
}

check_patch() {
    patch $PATCH_OPTS --dry-run < "$PATCH_FILE" 2>&1
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
