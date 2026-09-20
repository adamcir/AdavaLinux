#!/bin/sh
set -eu

installer=tools/installer/install.c
helpers=tools/installer/helpers.c
makefile=Makefile

# GRUB comes from Fedora through the current SystemPackager.
grep -Fq '"grub2-pc"' "$installer"
grep -Fq '"grub2-efi-x64"' "$installer"
grep -Fq '"grub2-efi-x64-modules"' "$installer"
grep -Fq '"--source"' "$helpers"
grep -Fq '"fedora"' "$helpers"

if grep -Fq 'SYSPCKG_PACKAGE_DIR' "$makefile"; then
  printf '%s\n' 'legacy local .syspckg GRUB bundle is still configured' >&2
  exit 1
fi

printf '%s\n' 'Fedora GRUB2 installer dependency test passed'
