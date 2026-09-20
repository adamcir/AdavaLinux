#!/bin/sh
set -eu

installer=tools/installer/install.c

# Current AdavaLinux uses Fedora RPM GRUB2 packages, not legacy .syspckg
# GRUB archives. The installer must resolve standard GRUB module paths.
grep -Fq '"/usr/lib/grub",' "$installer"
grep -Fq '"/usr/lib64/grub",' "$installer"
grep -Fq '"grub2-pc"' "$installer"
grep -Fq '"grub2-efi-x64"' "$installer"
grep -Fq '"grub2-efi-x64-modules"' "$installer"
grep -Fq '"grub2-install"' "$installer"

if grep -Fq 'grub-install-modules' "$installer"; then
  printf '%s\n' 'installer still reads a standalone GRUB module tree' >&2
  exit 1
fi

printf '%s\n' 'Fedora GRUB2 module tree test passed'
