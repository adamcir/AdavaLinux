# Framebuffer Hybrid Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add hybrid framebuffer and DRM kernel support so AdavaLinux can host simple direct-framebuffer apps in both QEMU and on real hardware without adding a GUI stack.

**Architecture:** Keep the existing minimalist rootfs unchanged for graphics userspace and implement the feature as a kernel-configuration contract. Test for the required hybrid graphics options first, then extend `filesforlinux/kernel.config` with VT, framebuffer console, DRM/KMS, `simpledrm`, and QEMU-friendly fallback drivers.

**Tech Stack:** Linux kernel config fragment, POSIX shell tests, existing build flow.

---

### Task 1: Add a failing kernel-config test

**Files:**
- Create: `tests/kernel_framebuffer_hybrid_config_test.sh`

**Step 1: Write the failing test**

Create a shell test that asserts `filesforlinux/kernel.config` contains:

- `CONFIG_VT=y`
- `CONFIG_TTY=y`
- `CONFIG_FB=y`
- `CONFIG_FRAMEBUFFER_CONSOLE=y`
- `CONFIG_DRM=y`
- `CONFIG_DRM_KMS_HELPER=y`
- `CONFIG_DRM_SIMPLEDRM=y`
- `CONFIG_DRM_BOCHS=y`
- `CONFIG_FB_VESA=y`

**Step 2: Run test to verify it fails**

Run: `sh tests/kernel_framebuffer_hybrid_config_test.sh`

Expected: FAIL because the current config fragment lacks the graphics options.

### Task 2: Add the minimal hybrid graphics config

**Files:**
- Modify: `filesforlinux/kernel.config`

**Step 1: Add VT and framebuffer core options**

Set:

- `CONFIG_VT=y`
- `CONFIG_TTY=y`
- `CONFIG_FB=y`
- `CONFIG_FRAMEBUFFER_CONSOLE=y`

**Step 2: Add DRM/KMS fallback options**

Set:

- `CONFIG_DRM=y`
- `CONFIG_DRM_KMS_HELPER=y`
- `CONFIG_DRM_SIMPLEDRM=y`

**Step 3: Add QEMU and legacy fallback drivers**

Set:

- `CONFIG_DRM_BOCHS=y`
- `CONFIG_FB_VESA=y`

### Task 3: Verify

**Files:**
- Test: `tests/kernel_framebuffer_hybrid_config_test.sh`

**Step 1: Run targeted test**

Run: `sh tests/kernel_framebuffer_hybrid_config_test.sh`

Expected: PASS

**Step 2: Run a broader sanity check**

Run: `sh -n build.sh`

Expected: PASS
