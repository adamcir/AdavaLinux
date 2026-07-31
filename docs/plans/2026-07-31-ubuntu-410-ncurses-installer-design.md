# Ubuntu 4.10 Style Ncurses Installer Design

**Goal:** Build a standalone C installer in `tools/installer` that preserves the behavior of `install.sh` while presenting a classic Ubuntu 4.10 style text UI.

**Approach:** The installer is a single C binary using `ncurses`. It does not call `install.sh`; it performs the same sequence directly and uses `fork`/`exec` for external system tools.

**UI:** Sequential wizard with blue background, gray dialogs, highlighted menu rows, button labels, and a progress/log screen.

**Flow:** Welcome, target disk, boot mode, ACPI mode, partition size, user identity, destructive confirmation, summary, install progress, result.

**Behavior To Preserve:** Disk validation, installer media detection, root-media safety checks, BIOS and UEFI partitioning, ext2 root formatting, optional FAT32 EFI formatting, initramfs extraction, user and root password hashing, hostname files, kernel copy, GRUB config generation, GRUB installation, sync and unmount cleanup.

**Testing:** Unit tests cover pure helpers: supported disk names, partition naming, username validation, progress formatting, and command argument construction. Manual destructive install testing remains required in QEMU.
