# Disable Fb Keep Drm Design

**Goal:** Remove the kernel framebuffer stack while keeping DRM enabled for SDL2-style graphics paths.

**Scope:** This change is limited to the kernel config fragment. It does not change boot arguments, init scripts, or serial/local getty configuration.

**Design:**
- Disable `CONFIG_FB` to remove the framebuffer core.
- Keep `CONFIG_FRAMEBUFFER_CONSOLE` disabled.
- Disable framebuffer-specific fallback drivers such as `CONFIG_FB_VESA`.
- Keep `CONFIG_DRM`, `CONFIG_DRM_KMS_HELPER`, and `CONFIG_DRM_SIMPLEDRM` enabled so userspace can still use DRM/KMS-backed rendering paths.
