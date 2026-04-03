# Disable Fbcon Keep Fb Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Turn off framebuffer console support while preserving framebuffer support for SDL2-oriented graphics use.

**Architecture:** The kernel config fragment already enables framebuffer support. The minimal change is to disable only `CONFIG_FRAMEBUFFER_CONSOLE` and leave `CONFIG_FB` enabled, then lock that contract with a focused shell test.

**Tech Stack:** Linux kernel config fragment, POSIX shell tests.

---

### Task 1: Add a failing kernel-config test

**Files:**
- Create: `tests/kernel_fbcon_disabled_test.sh`

**Step 1: Write the failing test**
- Assert `filesforlinux/kernel.config` contains `CONFIG_FB=y`.
- Assert `filesforlinux/kernel.config` does not contain `CONFIG_FRAMEBUFFER_CONSOLE=y`.
- Assert `filesforlinux/kernel.config` contains `# CONFIG_FRAMEBUFFER_CONSOLE is not set`.

**Step 2: Run test to verify it fails**

Run: `sh tests/kernel_fbcon_disabled_test.sh`
Expected: FAIL because `CONFIG_FRAMEBUFFER_CONSOLE` is still enabled.

### Task 2: Disable fbcon and keep framebuffer support

**Files:**
- Modify: `filesforlinux/kernel.config`

**Step 1: Update the config fragment**
- Keep `CONFIG_FB=y`.
- Replace `CONFIG_FRAMEBUFFER_CONSOLE=y` with `# CONFIG_FRAMEBUFFER_CONSOLE is not set`.

### Task 3: Verify

**Files:**
- Test: `tests/kernel_fbcon_disabled_test.sh`

**Step 1: Run the targeted test**

Run: `sh tests/kernel_fbcon_disabled_test.sh`
Expected: PASS
