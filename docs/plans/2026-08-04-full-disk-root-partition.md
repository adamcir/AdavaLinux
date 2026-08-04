# Full-Disk Root Partition Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Ensure an empty root partition size fills the remaining target disk space.

**Architecture:** Keep size interpretation in `installer_build_fdisk_script`, where both boot modes already produce their fdisk command stream. Normalize an empty size to fdisk's `+0` end-of-disk value and make the UI describe that established behavior.

**Tech Stack:** C11, static linker, ncurses, fdisk shell input, Make.

---

### Task 1: Cover the empty-size partition scripts

**Files:**
- Modify: `tools/installer/tests/test_helpers.c:89-99`
- Test: `tools/installer/tests/test_helpers.c`

**Step 1: Write the failing test**

Add assertions that an empty BIOS size creates the command ending in `echo '+0'`, and an empty UEFI size does the same for partition 2.

**Step 2: Run test to verify it fails**

Run: `make -C tools/installer test`

Expected: FAIL because the current script emits an empty fdisk size argument.

**Step 3: Implement the minimal code**

In `installer_build_fdisk_script`, set `size` to `+0` when `part_size` is null or empty; retain nonempty values exactly as entered.

**Step 4: Run test to verify it passes**

Run: `make -C tools/installer test`

Expected: `helper tests passed` and exit code 0.

### Task 2: Make the installer wording explicit

**Files:**
- Modify: `tools/installer/ui.c:650-660,820-827`

**Step 1: Update the prompt and summary text**

Change the empty-field description to state that it uses all remaining disk space. Keep explicit fdisk-size input supported.

**Step 2: Build the installer**

Run: `make -C tools/installer`

Expected: successful static installer build.

### Task 3: Verify the change

**Files:**
- Verify: `tools/installer/tests/test_helpers.c`
- Verify: `tools/installer/ui.c`

**Step 1: Run automated helper tests**

Run: `make -C tools/installer test`

Expected: `helper tests passed` and exit code 0.

**Step 2: Build the installer**

Run: `make -C tools/installer`

Expected: exit code 0.

**Step 3: Review the diff**

Run: `git diff -- tools/installer/helpers.c tools/installer/ui.c tools/installer/tests/test_helpers.c`

Expected: only the full-disk default, its tests, and the matching UI text are present.
