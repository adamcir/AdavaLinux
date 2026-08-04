#!/bin/sh
set -eu

makefile=Makefile
installer=tools/installer/install.c

# GRUB must read its module tree from the live initramfs.  Reading it from the
# mounted installer ISO caused EIO on the target UEFI boot path.
grep -Fq 'cp -a "$$GRUB_X86_64_EFI_DIR" "$$ROOTFS_DIR/grub-install-modules/x86_64-efi"' "$makefile"
grep -Fq '"/grub-install-modules",' "$installer"

printf '%s\n' 'live GRUB module tree test passed'
