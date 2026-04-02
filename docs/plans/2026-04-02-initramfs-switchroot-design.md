# Initramfs Switchroot Design

> Current worktree status is tracked in [2026-04-02-boot-console-worktree-state.md](/home/adam/Dokumenty/Projekty/Linux/AdavaLinux/docs/plans/2026-04-02-boot-console-worktree-state.md). This design document describes the earlier intended end state and no longer matches every filename or implementation detail in the current tree.

**Date:** 2026-04-02

**Goal:** Make the installed AdavaLinux boot through an initramfs that mounts the real root filesystem from disk and then hands off with `switch_root`.

## Current State

`build.sh` currently creates a single `initramfs.gz` from `filesforlinux/rootfs`.
The installer then extracts that same archive onto the target disk as the installed root filesystem.
This mixes two separate concerns:

- installer runtime used by the ISO at boot
- boot runtime used by the installed system before the root filesystem is mounted

That coupling makes it hard to add early-boot recovery behavior without also affecting installation layout.

## Chosen Approach

Generate two separate initramfs artifacts:

- `initramfs-installer.gz` for the live installer ISO
- `initramfs-system.gz` for the installed system

The installed GRUB configuration will always load the kernel plus `initramfs-system.gz`.
Its `/init` script will:

1. mount `/proc`, `/sys`, and `/dev`
2. parse the kernel command line for `root=` and `rootfstype=`
3. wait for the target block device if needed
4. mount the real root filesystem under `/newroot`
5. `exec switch_root /newroot /sbin/init`

If any of the required steps fail, `/init` will intentionally drop into a clearly marked fallback shell instead of continuing silently.

## Fallback Shell Behavior

The fallback mode must be obviously different from a normal boot:

- print a visible banner such as `INITRAMFS FALLBACK`
- print the reason for failure, for example root device missing or mount failure
- set `PS1='[initramfs-fallback] # '`
- start an interactive shell from BusyBox

This makes it clear that the system did not switch to the real root filesystem and is still running from initramfs.

## Installer Changes

The installer must stop unpacking the live installer ramdisk as the final installed root filesystem.
Instead it should install the actual system rootfs payload separately, then copy:

- the kernel into `/boot`
- `initramfs-system.gz` into `/boot`

The generated installed GRUB configuration must include both:

- `linux /boot/<kernel> root=... rootfstype=...`
- `initrd /boot/initramfs-system.gz`

## Build Changes

`build.sh` should be reorganized so it can assemble:

- installer root tree with installer-only content such as `root/install.sh`
- system initramfs root tree with the `/init` script and minimal binaries needed for early boot and fallback shell

The ISO GRUB config should boot the installer artifact.
The installed GRUB config written by `filesforlinux/rootfs/root/install.sh` should boot the system artifact.

## Testing

Add shell-based tests that verify:

- the installed GRUB config generator writes an `initrd /boot/initramfs-system.gz` line
- the installer copies `initramfs-system.gz` into the installed `/boot`
- the system `/init` contains explicit fallback-shell markers
- the build output contains separate installer and system initramfs artifacts

## Non-Goals

- automatic root discovery without a `root=` kernel argument
- advanced initramfs hooks, module loading, RAID, LVM, or encrypted root
- replacing BusyBox init in the final root filesystem
