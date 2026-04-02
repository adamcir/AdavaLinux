# Initramfs Switchroot Implementation Plan

> Current worktree status is tracked in [2026-04-02-boot-console-worktree-state.md](/home/adam/Dokumenty/Projekty/Linux/AdavaLinux/docs/plans/2026-04-02-boot-console-worktree-state.md). This plan still reflects the earlier `initramfs-system.gz` naming and a broader split than the code currently implements.

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make installed AdavaLinux boot through a dedicated initramfs that mounts the disk root filesystem, falls back to a clearly marked emergency shell on failure, and then hands off with `switch_root`.

**Architecture:** Split the current single-archive flow into two boot artifacts: an installer initramfs for the ISO and a system initramfs for installed boots. Keep the installed root filesystem as a separate payload, teach the installer to place `initramfs-system.gz` in `/boot`, and add a dedicated `/init` that performs root mount plus `switch_root` with an explicit fallback shell.

**Tech Stack:** POSIX shell, BusyBox, GRUB config generation, `cpio`/`gzip`, shell-script tests.

---

### Task 1: Add failing tests for the new boot contract

**Files:**
- Create: `tests/build_initramfs_layout_test.sh`
- Create: `tests/install_sh_initramfs_system_test.sh`
- Create: `tests/system_init_test.sh`

**Step 1: Write the failing tests**
- Assert `build.sh` references separate installer and system initramfs output names.
- Assert `filesforlinux/rootfs/root/install.sh` copies `initramfs-system.gz` into `/mnt/root/boot`.
- Assert installed `grub.cfg` templates written by `install.sh` include an `initrd /boot/initramfs-system.gz` line.
- Assert the system init script contains visible fallback markers such as `INITRAMFS FALLBACK` and `[initramfs-fallback]`.

**Step 2: Run tests to verify they fail**

Run: `sh tests/build_initramfs_layout_test.sh && sh tests/install_sh_initramfs_system_test.sh && sh tests/system_init_test.sh`

Expected: FAIL because the code still builds one initramfs and the installed boot path does not use `initrd`.

### Task 2: Split build artifacts into installer and system payloads

**Files:**
- Modify: `build.sh`
- Create: `filesforlinux/initramfs-system/init`
- Create: `filesforlinux/initramfs-system/README.md`

**Step 1: Write minimal build changes**
- Add distinct output names for installer and system initramfs archives.
- Keep the current ISO boot path on an installer archive.
- Add a second assembly path for the system initramfs tree.

**Step 2: Ensure required early-boot binaries are present**
- Include BusyBox applets and anything needed for `mount`, `switch_root`, shell access, and device setup in the system initramfs tree.

**Step 3: Verify build script syntax**

Run: `sh -n build.sh`

Expected: PASS

### Task 3: Implement the system `/init` with fallback shell

**Files:**
- Create: `filesforlinux/initramfs-system/init`
- Test: `tests/system_init_test.sh`

**Step 1: Write the failing behavior-focused test updates**
- Extend the test to check for root cmdline parsing, `/newroot` mount attempt, `switch_root`, fallback banner, and fallback prompt.

**Step 2: Run the targeted test to confirm failure**

Run: `sh tests/system_init_test.sh`

Expected: FAIL because the file does not yet implement the required flow.

**Step 3: Write minimal implementation**
- Mount `/proc`, `/sys`, and `/dev`.
- Parse `root=` and optional `rootfstype=`.
- Wait for the block device when `root=` resolves to a real device path.
- Mount the target under `/newroot`.
- `exec switch_root /newroot /sbin/init`.
- On failure, print a clear fallback banner and `exec sh` with `PS1='[initramfs-fallback] # '`.

**Step 4: Re-run the targeted test**

Run: `sh tests/system_init_test.sh`

Expected: PASS

### Task 4: Teach the installer to deploy the system initramfs

**Files:**
- Modify: `filesforlinux/rootfs/root/install.sh`
- Test: `tests/install_sh_initramfs_system_test.sh`

**Step 1: Write the failing test assertions**
- Check that installation copies `initramfs-system.gz` from installer media to `/mnt/root/boot/initramfs-system.gz`.
- Check that installed GRUB config includes the matching `initrd` line in both normal and ACPI-off variants.

**Step 2: Run the targeted test to confirm failure**

Run: `sh tests/install_sh_initramfs_system_test.sh`

Expected: FAIL because the installer still unpacks the live initramfs and writes kernel-only GRUB entries.

**Step 3: Write minimal implementation**
- Stop treating the live initramfs as the installed rootfs source.
- Copy the separate system initramfs into `/mnt/root/boot`.
- Add `initrd /boot/initramfs-system.gz` to generated installed GRUB entries.

**Step 4: Re-run the targeted test**

Run: `sh tests/install_sh_initramfs_system_test.sh`

Expected: PASS

### Task 5: Point the ISO boot path at the installer artifact

**Files:**
- Modify: `build.sh`
- Modify: `filesforlinux/iso/boot/grub/grub.cfg`
- Test: `tests/build_initramfs_layout_test.sh`

**Step 1: Add installer artifact assertions**
- Confirm ISO staging copies `initramfs-installer.gz`.
- Confirm ISO GRUB config references the installer artifact explicitly.

**Step 2: Run the targeted test to confirm failure**

Run: `sh tests/build_initramfs_layout_test.sh`

Expected: FAIL because the ISO still references the old single `initramfs.gz`.

**Step 3: Write minimal implementation**
- Stage `initramfs-installer.gz` into the ISO.
- Update ISO GRUB template to use that filename.

**Step 4: Re-run the targeted test**

Run: `sh tests/build_initramfs_layout_test.sh`

Expected: PASS

### Task 6: Verify end to end

**Files:**
- Test: `tests/build_initramfs_layout_test.sh`
- Test: `tests/install_sh_initramfs_system_test.sh`
- Test: `tests/system_init_test.sh`

**Step 1: Run targeted tests**

Run: `sh tests/build_initramfs_layout_test.sh && sh tests/install_sh_initramfs_system_test.sh && sh tests/system_init_test.sh`

Expected: PASS

**Step 2: Run existing script checks**

Run: `sh -n build.sh && sh -n filesforlinux/rootfs/root/install.sh`

Expected: PASS

**Step 3: Run a real build if dependencies are available**

Run: `sh build.sh`

Expected: installer ISO contains installer initramfs, and output directory contains system initramfs artifact as a separate file.

**Step 4: Commit**

```bash
git add build.sh filesforlinux/iso/boot/grub/grub.cfg filesforlinux/rootfs/root/install.sh filesforlinux/initramfs-system/init tests docs/plans
git commit -m "feat: boot installed system via initramfs switch_root"
```
