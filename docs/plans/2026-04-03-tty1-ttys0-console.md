# TTY1 And TTYS0 Console Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Keep only `tty1` and `ttyS0` active as boot/login consoles while preserving `nomodeset`.

**Architecture:** The kernel command line already declares both consoles in the correct order, so the change stays in userspace. The implementation keeps explicit device-node creation for `tty1` and `ttyS0` in both init stages and trims BusyBox `inittab` to just those two gettys.

**Tech Stack:** POSIX shell, BusyBox init/inittab, shell tests.

---

### Task 1: Add a failing console configuration test

**Files:**
- Create: `tests/tty1_ttys0_console_test.sh`

**Step 1: Write the failing test**
- Assert `filesforlinux/initramfs-disk/init` creates `/dev/tty1` and `/dev/ttyS0`.
- Assert `filesforlinux/rootfs/init` creates `/dev/tty1` and `/dev/ttyS0`.
- Assert `filesforlinux/rootfs/etc/inittab` contains `tty1` and `ttyS0` entries.
- Assert `filesforlinux/rootfs/etc/inittab` does not contain `tty2..tty7` entries.
- Assert `filesforlinux/rootfs/root/install.sh` keeps `console=ttyS0 console=tty1 nomodeset`.

**Step 2: Run test to verify it fails**

Run: `sh tests/tty1_ttys0_console_test.sh`
Expected: FAIL because `inittab` still contains `tty2..tty7`.

### Task 2: Narrow console management to tty1 and ttyS0

**Files:**
- Modify: `filesforlinux/rootfs/etc/inittab`

**Step 1: Remove extra virtual-terminal gettys**
- Keep the existing `tty1` respawn entry.
- Keep the existing `ttyS0` respawn entry.
- Remove `tty2..tty7` respawn entries.

### Task 3: Verify

**Files:**
- Test: `tests/tty1_ttys0_console_test.sh`
- Test: `filesforlinux/initramfs-disk/init`
- Test: `filesforlinux/rootfs/init`
- Test: `filesforlinux/rootfs/etc/inittab`

**Step 1: Run targeted test**

Run: `sh tests/tty1_ttys0_console_test.sh`
Expected: PASS

**Step 2: Run shell syntax checks**

Run: `sh -n filesforlinux/initramfs-disk/init && sh -n filesforlinux/rootfs/init && sh -n filesforlinux/rootfs/root/install.sh`
Expected: PASS
