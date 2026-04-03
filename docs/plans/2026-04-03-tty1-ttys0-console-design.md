# TTY1 And TTYS0 Console Design

**Goal:** Keep `ttyS0` usable for serial access and make `tty1` the only required local Linux console while preserving `nomodeset`.

**Scope:** This design intentionally does not attempt to enable `tty2..tty7`. It keeps the existing dual-console kernel command line and narrows userspace console management to `tty1` and `ttyS0` only.

**Design:**
- Keep the installed-system kernel arguments in the current order: `console=ttyS0 console=tty1 nomodeset`.
- Ensure both early init (`filesforlinux/initramfs-disk/init`) and final root init (`filesforlinux/rootfs/init`) always create `/dev/tty1` and `/dev/ttyS0`.
- Reduce BusyBox init respawn entries to only `tty1` and `ttyS0` so the booted system manages only the consoles explicitly required by the user.
- Verify the behavior with a targeted shell test plus shell syntax checks.
