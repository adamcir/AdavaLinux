# UEFI tty1 fbcon Design

**Goal:** Restore visible `tty1` text output when AdavaLinux boots in QEMU via UEFI.

**Scope:** This change is limited to the kernel config fragment and a focused shell test. It does not alter boot arguments, init scripts, or getty wiring.

## Root Cause

UEFI QEMU boots through a framebuffer or DRM path instead of legacy VGA text mode. The current kernel config fragment keeps `CONFIG_VGA_CONSOLE=y` and `CONFIG_DRM_SIMPLEDRM=y`, but it does not request the framebuffer text console backend. As a result, `tty1` can exist as a device and getty can run on it, yet nothing renders onto the graphical display.

## Chosen Approach

Enable:

- `CONFIG_FB=y`
- `CONFIG_FRAMEBUFFER_CONSOLE=y`

Keep the existing DRM options intact so UEFI still has a simple display path while Linux virtual terminals regain a visible text console on the framebuffer-backed display.

## Testing

Add a narrow shell test that asserts the kernel config fragment contains the framebuffer core and framebuffer console options required for visible `tty1` output in UEFI.
