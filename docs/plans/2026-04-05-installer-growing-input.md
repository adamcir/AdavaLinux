# Installer Growing Input Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make installer input dialogs grow downward across multiple wrapped lines so the visible cursor stays where the user is typing, then shrink back when trailing wrapped lines become empty.

**Architecture:** Add a small pure layout helper in `tools/installer/installer_logic.c` that computes wrapped line count and cursor position from text length and field width. Use that helper from `tools/installer/main.c` to render a variable-height input area and to recenter the window whenever the wrapped height changes.

**Tech Stack:** C, ncurses, existing installer test binary in `tools/installer/tests/test_logic.c`

---

### Task 1: Lock the desired wrapping behavior with a failing test

**Files:**
- Modify: `tools/installer/tests/test_logic.c`
- Modify: `tools/installer/installer_logic.h`

**Step 1: Write the failing test**

Add a test that verifies:

- `"abcd"` in width `4` uses `1` line and cursor `(0, 4)`
- `"abcde"` in width `4` uses `2` lines and cursor `(1, 1)`
- `"abcd"` after deleting back from `"abcde"` returns to `1` line

**Step 2: Run test to verify it fails**

Run: `make -C tools/installer test`
Expected: FAIL because the input layout helper does not exist yet.

### Task 2: Implement the minimal input layout helper

**Files:**
- Modify: `tools/installer/installer_logic.h`
- Modify: `tools/installer/installer_logic.c`

**Step 1: Write minimal implementation**

Add a helper that accepts:

- `text_length`
- `field_width`
- output pointers for wrapped line count, cursor row, and cursor column

Use simple character-based wrapping with a minimum of one visible line.

**Step 2: Run test to verify it passes**

Run: `make -C tools/installer test`
Expected: PASS

### Task 3: Use the helper in the ncurses input dialog

**Files:**
- Modify: `tools/installer/main.c`

**Step 1: Update input dialog layout**

Change `run_input_screen()` to:

- derive field line count and cursor row/column from the helper
- grow `height` as the field wraps
- move the help text below the last wrapped input row
- recenter the window after each size change

**Step 2: Keep current edit semantics**

Preserve:

- append-only typing
- backspace deletion
- masked password display
- Enter/Left/Esc behavior

**Step 3: Run focused verification**

Run: `make -C tools/installer test`
Expected: PASS

### Task 4: Build the installer binary with the updated UI

**Files:**
- Modify: `tools/installer/main.c`

**Step 1: Build the installer**

Run: `make -C tools/installer`
Expected: PASS and produce `tools/installer/installer`

**Step 2: Commit**

```bash
git add docs/plans/2026-04-05-installer-growing-input-design.md docs/plans/2026-04-05-installer-growing-input.md tools/installer/main.c tools/installer/installer_logic.c tools/installer/installer_logic.h tools/installer/tests/test_logic.c
git commit -m "fix: grow installer input field across lines"
```
