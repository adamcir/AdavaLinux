# GRUB mkconfig Flow Design

## Goal

Unify installed-system GRUB generation through GRUB package scripts and `grub-mkconfig`, while keeping the ISO installer media menu as four direct entries for its single kernel.

## Design

The ISO keeps a static `filesforlinux/iso/boot/grub/grub.cfg` with four normal `menuentry` entries: normal, debug, ACPI off, and ACPI off plus debug.

The installed system no longer has installer-specific or syspckg-specific C code that writes the final GRUB menu. The GRUB package provides the menu generator under `/etc/grub.d/10_adavalinux`, and `grub-mkconfig -o /boot/grub/grub.cfg` produces the final menu.

The generated installed-disk menu has one top-level latest-kernel entry named `AdavaLinux v<version>` and one submenu named `Advanced options for AdavaLinux v<version>`. The submenu lists every `/boot/vmlinuz-*` kernel, sorted by version descending, with normal and debug entries for each kernel.

The installer installs the correct GRUB package before bootloader setup based on the selected boot mode: `grub-efi` for UEFI and `grub-bios` for BIOS. After copying the kernel and disk initramfs, it calls the GRUB package's `grub-mkconfig` flow for the mounted target root.

`syspckg update --kernel` detects the target boot type, ensures the matching GRUB package is installed through syspckg, refreshes `/boot/vmlinuz` and `/boot/initramfs-disk.gz` symlinks, then calls `grub-mkconfig -o /boot/grub/grub.cfg`.

## Tests

Add installer tests that assert bootloader config generation uses `grub-mkconfig` instead of direct `grub.cfg` writing. Add syspckg tests that assert kernel update installs the correct GRUB package for BIOS/UEFI roots and calls `grub-mkconfig`.
