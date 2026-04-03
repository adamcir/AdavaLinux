# Kernel Config Disable Parser Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Fix disabled-option parsing in the kernel config build path and express the current framebuffer-related disables explicitly.

**Architecture:** `build.sh` currently ignores `# CONFIG_FOO is not set` lines, so some intended disables never reach the generated kernel `.config`. The fix adds explicit support for that standard syntax and keeps the active fragment on `CONFIG_*=n` for clarity, with shell tests locking both expectations down.

**Tech Stack:** POSIX shell, kernel config fragment parsing, shell tests.

---

### Task 1: Add a failing parser regression test

**Files:**
- Create: `tests/build_kernel_config_parser_test.sh`

**Step 1: Write the failing test**
- Assert `build.sh` still supports `CONFIG_FOO=n`.
- Assert `build.sh` also recognizes `# CONFIG_FOO is not set`.

**Step 2: Run test to verify it fails**

Run: `sh tests/build_kernel_config_parser_test.sh`
Expected: FAIL because `build.sh` does not yet handle `# CONFIG_FOO is not set`.

### Task 2: Fix parser and fragment format

**Files:**
- Modify: `build.sh`
- Modify: `filesforlinux/kernel.config`
- Modify: `tests/kernel_fbcon_disabled_test.sh`
- Modify: `tests/kernel_fb_disabled_keep_drm_test.sh`

**Step 1: Extend `build.sh` parser**
- Add a case branch that maps `# CONFIG_FOO is not set` to `scripts/config --disable CONFIG_FOO`.

**Step 2: Make disables explicit in the fragment**
- Replace commented-out `CONFIG_FB`, `CONFIG_FRAMEBUFFER_CONSOLE`, and `CONFIG_FB_VESA` lines with explicit `=n`.

**Step 3: Align existing tests**
- Update the focused kernel-config tests to assert the explicit `=n` representation.

### Task 3: Verify

**Files:**
- Test: `tests/build_kernel_config_parser_test.sh`
- Test: `tests/kernel_fbcon_disabled_test.sh`
- Test: `tests/kernel_fb_disabled_keep_drm_test.sh`
- Test: `build.sh`

**Step 1: Run the targeted tests**

Run: `sh tests/build_kernel_config_parser_test.sh && sh tests/kernel_fbcon_disabled_test.sh && sh tests/kernel_fb_disabled_keep_drm_test.sh`
Expected: PASS

**Step 2: Run shell syntax checks**

Run: `sh -n build.sh`
Expected: PASS
