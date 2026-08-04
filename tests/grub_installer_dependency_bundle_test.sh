#!/bin/sh
set -eu

makefile=Makefile

grep -Fq 'SYSPCKG_PACKAGE_DIR ?= $(PROJECT_DIR)/../syspckg/packages' "$makefile"
grep -Fq 'grub-bios grub-efi brotli bzip2 freetype libpng xz zlib' "$makefile"
grep -Fq 'Missing installer GRUB dependency package:' "$makefile"
grep -Fq 'cp -f "$$pkg" "$$ROOTFS_DIR/usr/share/syspckg/packages/"' "$makefile"

printf '%s\n' 'GRUB installer dependency bundle test passed'
