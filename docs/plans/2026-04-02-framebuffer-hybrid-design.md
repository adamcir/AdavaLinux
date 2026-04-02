# Framebuffer Hybrid Boot Design

> **Status:** Approved in chat on 2026-04-02 for implementation.

**Goal:** Enable simple graphics without a GUI stack by making AdavaLinux boot with framebuffer-capable kernel support that works in both QEMU and on real hardware.

**Context:** The current tree provides text consoles only. Rootfs already mounts `devtmpfs` and runs `mdev`, so device nodes such as `/dev/fb0` or `/dev/dri/card0` can appear automatically once the kernel exposes them. There is no X11, Wayland, or SDL stack in the image, which matches the target of direct framebuffer applications.

## Approaches Considered

### 1. fbdev-only

Enable only the classic framebuffer stack and target `/dev/fb0` directly.

- Pros: Smallest conceptual change and the simplest app model.
- Cons: Weakest portability across modern real hardware and not the preferred direction for newer kernels.

### 2. DRM/KMS-only

Rely only on DRM/KMS drivers and expect applications to use DRM directly.

- Pros: Modern kernel path and best long-term base.
- Cons: More work for the first non-GUI application and less convenient for a minimal direct framebuffer workflow.

### 3. Hybrid fallback

Enable VT plus framebuffer console support, add modern DRM/KMS fallback drivers, and keep classic framebuffer compatibility paths that help in QEMU and older environments.

- Pros: Best chance of booting with visible graphics in both QEMU and real hardware while preserving a simple `/dev/fb0` path when available.
- Cons: Slightly broader kernel configuration.

## Chosen Design

Use the hybrid fallback approach.

The kernel configuration will gain:

- VT and console support needed for visible local display
- framebuffer core plus framebuffer console
- DRM core and KMS helper support
- `simpledrm` as a generic modern fallback
- QEMU-friendly drivers such as `bochs`
- conservative classic fallback support such as VESA framebuffer where available

No userspace graphics stack will be added. Rootfs device handling is already sufficient. The immediate implementation goal is to codify the expected kernel options and keep them under test.

## Testing Strategy

Add a shell test that reads `filesforlinux/kernel.config` and asserts the required hybrid graphics options are present. Run the test first to verify it fails, then update the config until it passes.
