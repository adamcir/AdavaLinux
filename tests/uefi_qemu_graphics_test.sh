#!/usr/bin/env sh
set -eu

grep -Fq 'QEMU_UEFI_VIDEO ?= std' Makefile
test "$(grep -c -- '-vga "$(QEMU_UEFI_VIDEO)"' Makefile)" -eq 3
test "$(grep -c -- '-device virtio-vga' Makefile)" -eq 0

printf '%s\n' 'UEFI QEMU graphics configuration test passed'
