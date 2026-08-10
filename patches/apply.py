#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Apply / check / revert patches in this directory to managed_components.

Cross-platform (Linux / macOS / Windows):
  - Requires Python 3 (stdlib only) and git (required by ESP-IDF, so no extra deps).
  - Uses `git apply` (no fuzz) -- patches must match the component versions exactly.
    If a component version is bumped, patches must be regenerated.

Usage:
    python patches/apply.py            # apply all patches
    python patches/apply.py --check    # dry-run: report per-patch status, change nothing
    python patches/apply.py --revert   # revert all patches
"""

import argparse
import os
import subprocess
import sys

PATCH_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(PATCH_DIR)
MC_ROOT = os.path.join(PROJECT_DIR, "managed_components")

# Manifest: (patch_file, component_dir_under_managed_components)
# All patches are required: any failure aborts with non-zero exit.
MANIFEST = [
    ("0001-esp_lvgl_adapter-frame-ready-callback.patch", "espressif__esp_lvgl_adapter"),
    ("0002-lvgl__lvgl-ime-pinyin-fixes.patch", "lvgl__lvgl"),
    ("0003-esp_lv_decoder-disable-png.patch", "espressif__esp_lv_decoder"),
]

EXIT_OK = 0
EXIT_FAIL = 1


def _run(cwd, args):
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True)


def git_apply(cwd, patch_path, reverse=False, check=False):
    args = ["git", "apply"]
    if check:
        args.append("--check")
    if reverse:
        args.append("--reverse")
    args += ["-p1", patch_path]
    return _run(cwd, args)


def classify(cwd, patch_path):
    """Return 'applicable' | 'already_applied' | 'conflict'."""
    if git_apply(cwd, patch_path, check=True).returncode == 0:
        return "applicable"
    if git_apply(cwd, patch_path, reverse=True, check=True).returncode == 0:
        return "already_applied"
    return "conflict"


def _err_tail(res, n=140):
    err = (res.stderr or "").strip().replace("\n", " ")
    return err[:n]


def main():
    parser = argparse.ArgumentParser(
        description="Apply patches to managed_components (git apply, cross-platform).")
    parser.add_argument("--check", action="store_true",
                        help="dry-run: report status, change nothing")
    parser.add_argument("--revert", action="store_true",
                        help="revert (reverse-apply) all patches")
    args = parser.parse_args()

    rows = []          # (patch, status, note)
    failed = False

    for patch, comp in MANIFEST:
        patch_path = os.path.join(PATCH_DIR, patch)
        cwd = os.path.join(MC_ROOT, comp)

        if not os.path.isfile(patch_path):
            rows.append((patch, "ERROR", "patch file missing"))
            failed = True
            continue
        if not os.path.isdir(cwd):
            rows.append((patch, "SKIP", "component dir missing: %s" % comp))
            failed = True
            continue

        if args.check:
            status = classify(cwd, patch_path)
            label = {"applicable": "APPLICABLE",
                     "already_applied": "ALREADY-APPLIED",
                     "conflict": "CONFLICT (version mismatch?)"}[status]
            rows.append((patch, label, ""))
            if status == "conflict":
                failed = True
            continue

        if args.revert:
            res = git_apply(cwd, patch_path, reverse=True)
            if res.returncode == 0:
                rows.append((patch, "REVERTED", ""))
            else:
                rows.append((patch, "SKIP", "not applied? %s" % _err_tail(res)))
            continue

        # default: apply
        status = classify(cwd, patch_path)
        if status == "already_applied":
            rows.append((patch, "ALREADY-APPLIED", "skipped"))
            continue
        if status == "conflict":
            rows.append((patch, "ERROR",
                         "component version mismatch -- see patches/README.md"))
            failed = True
            continue

        res = git_apply(cwd, patch_path)
        if res.returncode == 0:
            rows.append((patch, "APPLIED", ""))
        else:
            rows.append((patch, "ERROR", _err_tail(res)))
            failed = True

    # summary
    w = 52
    print("=" * (w + 26))
    print("%-*s  %-16s  %s" % (w, "Patch", "Status", "Note"))
    print("-" * (w + 26))
    for patch, status, note in rows:
        print("%-*s  %-16s  %s" % (w, patch, status, note))
    print("-" * (w + 26))

    if failed:
        print("✗ Some patches FAILED. See errors above.")
        return EXIT_FAIL
    print("✓ Done.")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
