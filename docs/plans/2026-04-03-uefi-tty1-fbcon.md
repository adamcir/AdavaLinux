# UEFI tty1 fbcon Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Restore visible `tty1` text output for UEFI QEMU boots by enabling framebuffer console support in the kernel config fragment.

**Architecture:** Keep the current boot flow and userspace console setup unchanged. Fix the issue at the kernel config layer by enabling the framebuffer core and framebuffer console, then lock that expectation with a focused shell test.

**Tech Stack:** POSIX shell, kernel config fragment

---

### Task 1: Add a failing contract test

**Files:**
- Create: `tests/kernel_uefi_tty1_fbcon_test.sh`

**Step 1: Write the failing test**

Create a shell test that requires:

- `CONFIG_FB=y`
- `CONFIG_FRAMEBUFFER_CONSOLE=y`
- `CONFIG_DRM=y`
- `CONFIG_DRM_SIMPLEDRM=y`

**Step 2: Run test to verify it fails**

Run: `sh tests/kernel_uefi_tty1_fbcon_test.sh`
Expected: FAIL because `filesforlinux/kernel.config` does not yet contain the framebuffer options.

### Task 2: Enable framebuffer text console support

**Files:**
- Modify: `filesforlinux/kernel.config`

**Step 1: Write minimal implementation**

Add:

- `CONFIG_FB=y`
- `CONFIG_FRAMEBUFFER_CONSOLE=y`

Keep the existing DRM lines unchanged.

**Step 2: Run test to verify it passes**

Run: `sh tests/kernel_uefi_tty1_fbcon_test.sh`
Expected: PASS

### Task 3: Verify the targeted contract

**Files:**
- Test: `tests/kernel_uefi_tty1_fbcon_test.sh`

**Step 1: Re-run the targeted verification**

Run: `sh tests/kernel_uefi_tty1_fbcon_test.sh`
Expected: PASS
