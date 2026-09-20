#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
makefile="$repo_dir/Makefile"
grub_cfg="$repo_dir/filesforlinux/iso/boot/grub/grub.cfg"

grep -Fq 'MEMTEST_BIOS_IMAGE ?=' "$makefile"
grep -Fq 'MEMTEST_UEFI_IMAGE ?=' "$makefile"
grep -Fq 'Missing Memtest86+ BIOS image' "$makefile"
grep -Fq 'Missing Memtest86+ UEFI image' "$makefile"
grep -Fq 'memtest86+.bin' "$makefile"
grep -Fq 'memtest86+x64.efi' "$makefile"

grep -Fq 'menuentry "AdavaLinux v1.0 (acpi off + debug)"' "$grub_cfg"
grep -Fq 'menuentry "Memtest86+ (BIOS)"' "$grub_cfg"
grep -Fq 'menuentry "Memtest86+ (UEFI)"' "$grub_cfg"
grep -Fq 'linux16 /boot/memtest86+.bin' "$grub_cfg"
grep -Fq 'linux /boot/memtest86+x64.efi' "$grub_cfg"
grep -Fq 'terminal_output console' "$grub_cfg"

test "$(grep -n 'menuentry "AdavaLinux v1.0 (acpi off + debug)"' "$grub_cfg" | cut -d: -f1)" -lt \
  "$(grep -n 'menuentry "Memtest86+ (BIOS)"' "$grub_cfg" | cut -d: -f1)"
test "$(grep -n 'menuentry "AdavaLinux v1.0 (acpi off + debug)"' "$grub_cfg" | cut -d: -f1)" -lt \
  "$(grep -n 'menuentry "Memtest86+ (UEFI)"' "$grub_cfg" | cut -d: -f1)"

installed_grub="$repo_dir/filesforlinux/rootfs/etc/grub.d/10_adavalinux"

grep -Fq 'submenu "Advanced options for AdavaLinux v$version"' "$installed_grub"
grep -Fq 'AdavaLinux v$version (debug)' "$installed_grub"
grep -Fq 'AdavaLinux v$version (acpi off = ideal for old computers)' "$installed_grub"
grep -Fq 'AdavaLinux v$version (acpi off + debug)' "$installed_grub"
grep -Fq 'menuentry "Memtest86+ (BIOS)"' "$installed_grub"
grep -Fq 'menuentry "Memtest86+ (UEFI)"' "$installed_grub"
grep -Fq 'linux16 /boot/memtest86+.bin' "$installed_grub"
grep -Fq 'linux /boot/memtest86+x64.efi' "$installed_grub"

printf '%s\n' 'Memtest86+ boot entries test passed'
