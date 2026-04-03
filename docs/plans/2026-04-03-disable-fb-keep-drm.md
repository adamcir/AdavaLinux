# Disable Fb Keep Drm Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Remove framebuffer support from the kernel config while keeping DRM support enabled.

**Architecture:** The current config already has fbcon disabled, so the next minimal experiment is to remove the framebuffer core and its VESA fallback while leaving the DRM path intact. A focused shell test will assert that `CONFIG_FB` and `CONFIG_FB_VESA` are disabled and DRM remains enabled.

**Tech Stack:** Linux kernel config fragment, POSIX shell tests.

---

### Task 1: Add a failing kernel-config test

**Files:**
- Create: `tests/kernel_fb_disabled_keep_drm_test.sh`

**Step 1: Write the failing test**
- Assert `filesforlinux/kernel.config` does not contain `CONFIG_FB=y`.
- Assert `filesforlinux/kernel.config` contains `# CONFIG_FB is not set`.
- Assert `filesforlinux/kernel.config` does not contain `CONFIG_FB_VESA=y`.
- Assert `filesforlinux/kernel.config` contains `# CONFIG_FB_VESA is not set`.
- Assert `filesforlinux/kernel.config` contains `CONFIG_DRM=y`.
- Assert `filesforlinux/kernel.config` contains `CONFIG_DRM_SIMPLEDRM=y`.

**Step 2: Run test to verify it fails**

Run: `sh tests/kernel_fb_disabled_keep_drm_test.sh`
Expected: FAIL because framebuffer support is still enabled.

### Task 2: Disable framebuffer and keep DRM

**Files:**
- Modify: `filesforlinux/kernel.config`

**Step 1: Update the config fragment**
- Replace `CONFIG_FB=y` with `# CONFIG_FB is not set`.
- Replace `CONFIG_FB_VESA=y` with `# CONFIG_FB_VESA is not set`.
- Keep the DRM options enabled.

### Task 3: Verify

**Files:**
- Test: `tests/kernel_fb_disabled_keep_drm_test.sh`

**Step 1: Run the targeted test**

Run: `sh tests/kernel_fb_disabled_keep_drm_test.sh`
Expected: PASS
