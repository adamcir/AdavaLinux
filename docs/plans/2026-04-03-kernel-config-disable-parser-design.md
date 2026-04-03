# Kernel Config Disable Parser Design

**Goal:** Make disabled kernel options in `filesforlinux/kernel.config` apply reliably during the build and express current framebuffer-related disables explicitly.

**Scope:** This change is limited to the kernel config fragment, the parser in `build.sh`, and focused shell tests. It does not change boot arguments or init/getty behavior.

**Design:**
- Extend the config-fragment parser in `build.sh` so it understands both `CONFIG_FOO=n` and `# CONFIG_FOO is not set`.
- Rewrite the currently relevant disabled kernel options in `filesforlinux/kernel.config` to explicit `=n` form so the fragment is unambiguous.
- Verify both behaviors with shell tests: one for parser support, one for the expected framebuffer/DRM config contract.
