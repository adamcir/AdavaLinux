# Memtest86+ Boot Entries Design

## Goal

Include separate Memtest86+ BIOS (`.bin`) and UEFI (`.efi`) payloads in both generated AdavaLinux ISO trees and expose them from the `acpi off + debug` GRUB menu branch.

## Architecture

The Makefile will accept configurable host paths for the two Memtest86+ artifacts, validate both files during ISO assembly, and copy them into `/boot` with stable names. The shared GRUB template will add two submenu entries under the existing `AdavaLinux v1.0 (acpi off + debug)` entry, using `linux16` for the BIOS binary and `linux` for the UEFI EFI-handoff image shipped by Debian/Ubuntu. A shell regression test will verify the template and Makefile contract without requiring a full ISO build.

## Error handling

ISO creation stops with an actionable error when either source artifact is missing, avoiding images whose menu advertises a non-existent tester. The default source locations follow common `memtest86+` package layouts; callers can override them with Make variables.

## Verification

Run the focused shell regression test and the existing GRUB-related tests. If the host provides both artifacts, run `make iso` and inspect the generated ISO tree for `/boot/memtest86+.bin` and `/boot/memtest86+x64.efi`.
