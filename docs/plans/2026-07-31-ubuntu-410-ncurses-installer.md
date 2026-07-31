# Ubuntu 4.10 Style Ncurses Installer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Create a standalone C/ncurses installer under `tools/installer` that follows the same installation behavior as `install.sh`.

**Architecture:** Keep pure validation/path helpers testable outside ncurses. Put UI in `ui.c`, installation orchestration in `install.c`, system command helpers in `sys.c`, and shared state in `installer.h`.

**Tech Stack:** C11, ncurses, POSIX process/file APIs, existing Makefile integration.

---

### Task 1: Add Build And Test Harness

**Files:**
- Create: `tools/installer/Makefile`
- Create: `tools/installer/tests/test_helpers.c`

**Steps:**
1. Add tests for helper behavior before implementation.
2. Run `make -C tools/installer test` and confirm it fails because source files are missing.
3. Add minimal build targets for `installer` and `test_helpers`.
4. Re-run tests after helper implementation.

### Task 2: Implement Pure Helpers

**Files:**
- Create: `tools/installer/installer.h`
- Create: `tools/installer/helpers.c`

**Steps:**
1. Implement username validation, disk filtering, partition naming, and progress bar formatting.
2. Run `make -C tools/installer test`.

### Task 3: Implement Ncurses Wizard

**Files:**
- Create: `tools/installer/ui.h`
- Create: `tools/installer/ui.c`
- Create: `tools/installer/main.c`

**Steps:**
1. Add Ubuntu 4.10 style colors and centered dialogs.
2. Add screens for welcome, disk, boot mode, ACPI mode, partition size, identity, confirm, summary, progress, and result.
3. Keep all destructive actions behind the final confirmation.

### Task 4: Port Install Orchestration

**Files:**
- Create: `tools/installer/sys.h`
- Create: `tools/installer/sys.c`
- Create: `tools/installer/install.h`
- Create: `tools/installer/install.c`

**Steps:**
1. Add command runner that streams output into the progress log.
2. Port the `install.sh` sequence without shell wrapper execution.
3. Generate `passwd`, `shadow`, `group`, `hostname`, `hosts`, and `grub.cfg` from C.

### Task 5: Verify

**Commands:**
- `make -C tools/installer test`
- `make -C tools/installer clean all`
- `make tools`

**Manual follow-up:** Boot installer ISO in QEMU before using on real disks.
