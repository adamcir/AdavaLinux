#!/bin/sh
set -eu

makefile=Makefile
installer=tools/installer/install.c

# GRUB modules must come from the selected local syspckg archive only after
# installation starts.  A standalone extracted tree would make the ISO and
# target system unnecessarily dirty.
if grep -Fq 'grub-install-modules' "$makefile"; then
  printf '%s\n' 'Makefile still creates a standalone GRUB module tree' >&2
  exit 1
fi
if grep -Fq 'grub-install-modules' "$installer"; then
  printf '%s\n' 'installer still reads a standalone GRUB module tree' >&2
  exit 1
fi
grep -Fq '"/usr/lib/grub",' "$installer"
grep -Fq '"/usr/lib64/grub",' "$installer"
grep -Fq 'for base in grub-bios grub-efi brotli bzip2 freetype libpng xz zlib; do' "$makefile"
grep -Fq '"$$ROOTFS_DIR/usr/share/syspckg/packages"' "$makefile"

tar -tJf ../syspckg/packages/grub-bios-2.12.syspckg 2>/dev/null | grep -Fq \
  'grub-bios-2.12/usr/lib/grub/i386-pc/modinfo.sh'
tar -tJf ../syspckg/packages/grub-bios-2.12.syspckg 2>/dev/null | grep -Fq \
  'grub-bios-2.12/usr/lib/grub/i386-pc/kernel.img'
tar -tJf ../syspckg/packages/grub-efi-2.12.syspckg 2>/dev/null | grep -Fq \
  'grub-efi-2.12/usr/lib/grub/x86_64-efi/modinfo.sh'
tar -tJf ../syspckg/packages/grub-efi-2.12.syspckg 2>/dev/null | grep -Fq \
  'grub-efi-2.12/usr/lib/grub/x86_64-efi/kernel.img'

printf '%s\n' 'GRUB package module tree test passed'
