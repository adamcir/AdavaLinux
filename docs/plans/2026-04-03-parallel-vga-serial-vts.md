# Parallel VGA And Serial VTs Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Keep `ttyS0` available for serial access while making local `tty1..tty7` usable as normal framebuffer-backed Linux virtual terminals.

**Architecture:** The existing BusyBox `inittab` already respawns `getty` on `tty1..tty7` and `ttyS0`, so the missing piece is reliable device-node creation in both the disk initramfs and the final rootfs init. The implementation keeps serial and VGA in parallel, explicitly creates `/dev/tty1` through `/dev/tty7` plus `/dev/ttyS0`, and verifies the expected VT and framebuffer kernel options remain enabled.

**Tech Stack:** POSIX shell, BusyBox init/inittab, Linux VT/framebuffer config, shell-script tests.

---

### Task 1: Add failing tests for VT device setup

**Files:**
- Create: `tests/rootfs_vt_console_test.sh`
- Create: `tests/initramfs_vt_console_test.sh`

**Step 1: Write the failing tests**
- Assert `filesforlinux/rootfs/init` creates `/dev/tty1` through `/dev/tty7` and `/dev/ttyS0`.
- Assert `filesforlinux/initramfs-disk/init` creates `/dev/tty1` through `/dev/tty7` and `/dev/ttyS0`.
- Assert `filesforlinux/rootfs/etc/inittab` contains respawn entries for `tty1..tty7` and `ttyS0`.
- Assert `filesforlinux/kernel.config` keeps `CONFIG_VT`, `CONFIG_TTY`, `CONFIG_FB`, and `CONFIG_FRAMEBUFFER_CONSOLE` enabled.

**Step 2: Run the tests to verify they fail**

Run: `sh tests/rootfs_vt_console_test.sh && sh tests/initramfs_vt_console_test.sh`
Expected: FAIL because `tty2..tty7` are not created yet.

### Task 2: Implement VT device creation

**Files:**
- Modify: `filesforlinux/rootfs/init`
- Modify: `filesforlinux/initramfs-disk/init`

**Step 1: Update final rootfs init**
- Replace the single `tty1` node creation with a small loop that creates `tty1..tty7`.

**Step 2: Update disk initramfs**
- Mirror the same `tty1..tty7` creation logic in `mount_essential_fs`.

### Task 3: Verify targeted behavior

**Files:**
- Test: `tests/rootfs_vt_console_test.sh`
- Test: `tests/initramfs_vt_console_test.sh`

**Step 1: Run targeted tests**

Run: `sh tests/rootfs_vt_console_test.sh && sh tests/initramfs_vt_console_test.sh`
Expected: PASS

**Step 2: Run shell syntax checks**

Run: `sh -n filesforlinux/rootfs/init && sh -n filesforlinux/initramfs-disk/init`
Expected: PASS
