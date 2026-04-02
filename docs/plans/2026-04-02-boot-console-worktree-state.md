# Boot And Console Worktree State

**Date:** 2026-04-02

This document records the actual boot and console changes currently present in the worktree.
It is meant as a reality check against the older plan documents in this directory, which still describe an earlier `initramfs-system.gz` direction.

## Files Changed

- `build.sh`
- `filesforlinux/iso/boot/grub/grub.cfg`
- `filesforlinux/rootfs/etc/inittab`
- `filesforlinux/rootfs/root/install.sh`
- `filesforlinux/initramfs-disk/README.md`
- `filesforlinux/initramfs-disk/init`
- `tests/initramfs_disk_fallback_console_test.sh`
- `tests/install_sh_serial_console_test.sh`
- `tests/install_sh_direct_disk_boot_test.sh`
- `tests/rootfs_serial_console_test.sh`

## Implemented Changes

### Split initramfs artifacts

`build.sh` now assembles and packs two separate archives:

- `initramfs-installer.gz`
- `initramfs-disk.gz`

The ISO staging step now copies both files into `iso/boot`, and the sanity checks validate both archives independently.

### Installer ISO now boots the installer archive explicitly

`filesforlinux/iso/boot/grub/grub.cfg` no longer references the old `initramfs.gz` filename.
All ISO menuentries now load:

- `linux /boot/__KERNEL_IMAGE__ rdinit=/init ...`
- `initrd /boot/initramfs-installer.gz`

### Installed system now loads a dedicated disk initramfs

`filesforlinux/rootfs/root/install.sh` now copies `initramfs-disk.gz` into the installed `/boot` directory and writes installed GRUB entries with:

- `root=... rootfstype=...`
- `initrd /boot/initramfs-disk.gz`

The installed boot entries also use:

- `console=ttyS0 console=tty1`

That ordering keeps serial logging enabled while making `tty1` the controlling `/dev/console` device.

### Dedicated disk initramfs template added

`filesforlinux/initramfs-disk/init` is a standalone early-boot script for installed systems.
Its current behavior is:

1. mount `/proc`, `/sys`, and `/dev`
2. parse `root=` and optional `rootfstype=` from `/proc/cmdline`
3. resolve `UUID=` and `LABEL=` through `blkid`
4. wait for block devices under `/dev/*`
5. mount the real root under `/newroot`
6. move `/dev`, `/proc`, and `/sys`
7. `exec switch_root /newroot /sbin/init`

On failure it enters a clearly marked fallback shell with the prompt:

- `[initramfs-fallback] # `

### Fallback console behavior changed

The current fallback shell behavior in `filesforlinux/initramfs-disk/init` is:

- write fallback banner to `tty1` when available
- otherwise write it to `ttyS0`
- avoid printing the same banner line through both explicit TTY writes and stdout
- start the main fallback shell on `tty1`
- when both `tty1` and `ttyS0` exist, also start a secondary shell on `ttyS0` via `setsid`

This was added so VGA stays interactive while serial access remains available for recovery/debugging.

### Rootfs getty layout expanded

`filesforlinux/rootfs/etc/inittab` now respawns BusyBox getty shells on:

- `tty1`
- `tty2`
- `tty3`
- `tty4`
- `tty5`
- `tty6`
- `tty7`
- `ttyS0`

## Current Gaps And Mismatches

### Installer extraction path still uses the installer initramfs as the installed rootfs payload

Despite the split archive work, `filesforlinux/rootfs/root/install.sh` still extracts:

- `/mnt/install/boot/initramfs-installer.gz`

directly onto `/mnt/root`.

That means the worktree has a dedicated disk initramfs for boot handoff, but it does **not** yet have a separately assembled installed root filesystem payload. The old design documents expected that extra split to happen, but the current code does not implement it yet.

### Older plan documents still mention `initramfs-system.gz`

The existing files:

- `docs/plans/2026-04-02-initramfs-switchroot-design.md`
- `docs/plans/2026-04-02-initramfs-switchroot.md`

describe an earlier artifact name and a broader end state than what is currently in the worktree.

### One existing test still follows the older naming

`tests/install_sh_direct_disk_boot_test.sh` still checks for:

- `initrd /boot/initramfs-system.gz`

That does not match the current implementation, which uses `initramfs-disk.gz`.

## Verified Script-Level Checks

The following targeted checks match the current worktree state:

- `sh tests/initramfs_disk_fallback_console_test.sh`
- `sh tests/install_sh_serial_console_test.sh`
- `sh tests/rootfs_serial_console_test.sh`

## Recommended Next Cleanup

To make the documentation and tests internally consistent, the next cleanup should do one of these:

1. Rename the implementation from `initramfs-disk.gz` to `initramfs-system.gz` everywhere.
2. Keep `initramfs-disk.gz` and update the older plans/tests to that final name.
3. Finish the original split fully by introducing a separate installed rootfs payload in addition to the disk initramfs.
