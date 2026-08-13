# GRUB Package Module Trees Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Supply GRUB platform modules solely through local SystemPackager packages and remove the redundant `/grub-install-modules` tree from the ISO and installed system.

**Architecture:** The build stops copying extracted GRUB modules into both ISO staging and the live initramfs. The installer installs the selected local GRUB package into its live RAM root before invoking `grub-install`, then resolves standard `/usr/lib/grub/<platform>` module directories. The target root receives its selected package through the existing root install path.

**Tech Stack:** GNU Make, POSIX shell, C installer, SystemPackager package trees, shell regression tests.

---

### Task 1: Package the platform module trees

**Files:**
- Modify: `../syspckg/grub-bios-2.12/usr/lib/grub/i386-pc/*`
- Create: `../syspckg/grub-efi-2.12/usr/lib/grub/x86_64-efi/*`
- Regenerate: `../syspckg/packages/grub-bios-2.12.syspckg`
- Regenerate: `../syspckg/packages/grub-efi-2.12.syspckg`

**Step 1:** Verify that both package source trees contain `modinfo.sh` and `kernel.img` at their normal platform paths.

**Step 2:** Populate the UEFI package tree from the build GRUB x86_64-efi module source, preserving the full tree.

**Step 3:** Build both `.syspckg` archives using the SystemPackager packaging workflow.

**Step 4:** Inspect the archives to confirm their platform module trees are present.

### Task 2: Test that the standalone module tree is absent

**Files:**
- Create: `tests/grub_package_module_tree_test.sh`
- Modify: `tests/run_tests.sh` (if it explicitly enumerates tests)

**Step 1:** Write a test that fails while Makefile still copies `grub-install-modules` into `ROOTFS_DIR` or `ISO_DIR`.

**Step 2:** Run the test and confirm the expected failure.

**Step 3:** Extend it to require package-based standard locations in the installer resolver.

### Task 3: Remove standalone copies and use package directories

**Files:**
- Modify: `Makefile:439-446,527-533`
- Modify: `tools/installer/install.c:188-204`

**Step 1:** Delete the Makefile blocks that copy module trees to `ROOTFS_DIR/grub-install-modules` and `ISO_DIR/grub-install-modules`.

**Step 2:** Delete the two standalone paths from `resolve_grub_platform_dir`; retain `/usr/lib/grub` and compatible standard fallbacks.

**Step 3:** Run the new test and existing GRUB regression tests.

### Task 4: Verify the build products

**Files:**
- Verify: generated installer initramfs and ISO staging tree

**Step 1:** Run the relevant test suite.

**Step 2:** Build the ISO target.

**Step 3:** Assert no `grub-install-modules` entry exists in the initramfs or ISO, and assert package archives carry the two platform module trees.

**Step 4:** Run the BIOS and UEFI installer smoke tests when the project environment makes them available.
