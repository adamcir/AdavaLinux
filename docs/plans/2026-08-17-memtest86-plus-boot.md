# Memtest86+ Boot Entries Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add Memtest86+ BIOS and UEFI payloads to generated AdavaLinux ISO images and expose them under the ACPI-off debug boot menu.

**Architecture:** Make ISO assembly validate configurable Memtest86+ source paths, copy the BIOS `.bin` and UEFI `.efi` files into `/boot`, and use the shared GRUB template to launch the matching payload. Debian/Ubuntu's UEFI artifact is a Linux boot image with EFI handoff, so it is loaded with GRUB's `linux` command. A focused shell test checks the build/menu contract.

**Tech Stack:** GNU Make, POSIX shell, GRUB configuration, Memtest86+ binaries.

---

### Task 1: Add a failing contract test

**Files:**
- Create: `tests/memtest86_boot_entries_test.sh`

**Steps:**
1. Assert the GRUB template contains both payload names, the ACPI-off debug branch, `linux16`, and `chainloader`/`boot`.
2. Assert the Makefile defines source overrides, validates both files, and copies both artifacts into the ISO `/boot` tree.
3. Run the test and confirm it fails before implementation.

### Task 2: Add Memtest86+ source handling to ISO assembly

**Files:**
- Modify: `Makefile`

**Steps:**
1. Define overridable `MEMTEST_BIOS_IMAGE` and `MEMTEST_UEFI_IMAGE` defaults.
2. Validate both files during ISO packaging with actionable errors.
3. Copy them as `memtest86+.bin` and `memtest86+x64.efi` into `ISO_DIR/boot`.
4. Run the focused test and confirm it passes.

### Task 3: Add ACPI-off debug menu entries

**Files:**
- Modify: `filesforlinux/iso/boot/grub/grub.cfg`

**Steps:**
1. Add BIOS and UEFI Memtest86+ entries immediately after the existing ACPI-off debug Linux entry.
2. Use `linux16 /boot/memtest86+.bin` for BIOS and `chainloader /boot/memtest86+x64.efi` followed by `boot` for UEFI.
3. Run the focused and existing GRUB tests.

### Task 4: Verify the generated artifacts

**Steps:**
1. Run all shell tests under `tests/`.
2. If both host payload files exist, run `make iso` and inspect the ISO tree for both `/boot` files.
3. Report any unavailable host artifact paths explicitly.
