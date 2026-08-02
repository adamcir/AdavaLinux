+# GRUB KMS Regeneration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ensure installer-generated and `syspckg update --kernel` GRUB configuration keeps KMS enabled, so X.Org can open `/dev/dri/card0` on BIOS and UEFI QEMU boots.

**Architecture:** The installer writes a complete `/etc/default/grub` without `nomodeset`, then uses the packaged GRUB generator to create `grub.cfg`. Kernel update follows the same contract: after replacing boot artifacts, it writes a complete known-good `/etc/default/grub` and runs `grub-mkconfig`; it does not mutate or retain legacy kernel arguments. UEFI display support remains a kernel-config contract (`EFI framebuffer`, `fbcon`, `simpledrm`, and `virtio_gpu`).

**Tech Stack:** C11, POSIX shell tests, GRUB, Linux kernel configuration.

---

### Task 1: Cover complete default-GRUB regeneration

**Files:**
- Modify: `tools/syspckg/tests/update_kernel_boot_test.sh`
- Test: `tools/syspckg/tests/update_kernel_boot_test.sh`

**Step 1:** Seed the fake root with a legacy `/etc/default/grub` containing `nomodeset`, and require `update --kernel` to replace it with the complete KMS-safe content.

**Step 2:** Run the test and verify it fails because the legacy file remains unchanged.

### Task 2: Regenerate default GRUB during kernel update

**Files:**
- Modify: `tools/syspckg/main.c`
- Test: `tools/syspckg/tests/update_kernel_boot_test.sh`

**Step 1:** Add a narrowly-scoped helper that writes `/etc/default/grub` from canonical fields: target root device argument, ext4 root filesystem, serial and local consoles, `libata.force=noncq`, and quiet default.

**Step 2:** Call it in the successful `update --kernel` path before `grub-mkconfig`.

**Step 3:** Re-run the kernel-update test and verify it passes.

### Task 3: Lock installer and UEFI graphics contracts

**Files:**
- Modify: `tools/installer/tests/test_helpers.c`
- Modify: `tools/installer/helpers.c`, `tools/installer/install.c`
- Create: `tests/kernel_uefi_kms_config_test.sh`

**Step 1:** Add helper coverage for the canonical installer GRUB content and assert it has no `nomodeset`.

**Step 2:** Make the installer use the canonical KMS-safe content.

**Step 3:** Add a shell regression test for EFI framebuffer, framebuffer console, `simpledrm`, and virtio GPU options in `filesforlinux/kernel.config`.

**Step 4:** Run installer, syspckg, and kernel-config tests.

