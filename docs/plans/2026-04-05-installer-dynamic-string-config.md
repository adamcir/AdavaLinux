# Installer Dynamic String Config Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Convert all installer text fields from fixed-size arrays to dynamic strings across the ncurses frontend and backend installer configuration.

**Architecture:** Move owned text fields in installer configs to `char *`, add a minimal set of helpers for replace/free operations, and route all user input and environment loading through those helpers. Keep string formatting scratch buffers fixed-size only where they are true local temporaries, not owned config state.

**Tech Stack:** C, ncurses, libc allocation helpers, existing installer test binary in `tools/installer/tests/test_logic.c`

---

### Task 1: Add failing tests for dynamic input storage helpers

**Files:**
- Modify: `tools/installer/tests/test_logic.c`
- Modify: `tools/installer/installer_logic.h`

**Step 1: Write the failing test**

Add tests that verify:

- creating an editable string from `NULL` yields an empty owned string
- appending past the original capacity grows the string
- deleting characters updates the visible length without truncation side effects

**Step 2: Run test to verify it fails**

Run: `make -C tools/installer test`
Expected: FAIL because the dynamic string helpers do not exist yet.

### Task 2: Implement minimal shared string helpers

**Files:**
- Modify: `tools/installer/installer_logic.h`
- Modify: `tools/installer/installer_logic.c`

**Step 1: Write minimal implementation**

Add helpers for:

- replacing an owned string with a duplicated value
- ensuring editable capacity for input growth
- freeing owned strings safely

**Step 2: Run test to verify it passes**

Run: `make -C tools/installer test`
Expected: PASS

### Task 3: Convert frontend installer config and input dialog

**Files:**
- Modify: `tools/installer/main.c`

**Step 1: Convert config fields**

Change text fields in `InstallerConfig` from fixed arrays to `char *`.

**Step 2: Update initialization and call sites**

Replace `safe_copy()`-based config writes with shared string helpers and `NULL`-safe reads.

**Step 3: Update `run_input_screen()`**

Change the input API to accept an owned dynamic string pointer and grow it as needed while preserving the multiline cursor behavior.

**Step 4: Run focused verification**

Run: `make -C tools/installer test`
Expected: PASS

### Task 4: Convert backend config loading and cleanup

**Files:**
- Modify: `tools/installer/install.c`
- Modify: `tools/installer/install.h`

**Step 1: Convert backend config fields**

Change owned backend strings to `char *`.

**Step 2: Update env loading**

Duplicate environment values into owned strings and apply defaults through helpers.

**Step 3: Add cleanup**

Free all owned backend strings on normal exit and failure paths before returning from the backend entrypoint.

**Step 4: Run focused verification**

Run: `make -C tools/installer test`
Expected: PASS

### Task 5: Rebuild the full installer

**Files:**
- Modify: `tools/installer/main.c`
- Modify: `tools/installer/install.c`
- Modify: `tools/installer/installer_logic.c`
- Modify: `tools/installer/installer_logic.h`
- Modify: `tools/installer/tests/test_logic.c`

**Step 1: Build the installer**

Run: `make -C tools/installer`
Expected: PASS and produce `tools/installer/installer`

**Step 2: Commit**

```bash
git add docs/plans/2026-04-05-installer-dynamic-string-config-design.md docs/plans/2026-04-05-installer-dynamic-string-config.md tools/installer/main.c tools/installer/install.c tools/installer/install.h tools/installer/installer_logic.c tools/installer/installer_logic.h tools/installer/tests/test_logic.c
git commit -m "refactor: use dynamic installer config strings"
```
