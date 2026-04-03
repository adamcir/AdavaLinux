# Disable Fbcon Keep Fb Design

**Goal:** Disable the framebuffer console while keeping framebuffer support available for SDL2-style applications.

**Scope:** This change only affects the kernel config fragment. It does not change boot arguments, `inittab`, `tty1`, or `ttyS0` userspace behavior.

**Design:**
- Keep `CONFIG_FB=y` so framebuffer-capable userspace still has kernel framebuffer support available.
- Disable `CONFIG_FRAMEBUFFER_CONSOLE` so the kernel does not bind text VT output to fbcon.
- Verify the result with a narrow shell test that asserts `CONFIG_FB=y` remains present and `CONFIG_FRAMEBUFFER_CONSOLE` is disabled.
