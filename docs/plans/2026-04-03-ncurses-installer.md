# Ncurses Installer Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a statically linked C installer with a `menuconfig`-style `ncurses` UI and integrate it into the AdavaLinux build and installer rootfs.

**Architecture:** Keep the installer as one binary with a clear split between UI rendering, wizard state, and installation logic. Reuse the current shell install flow as the behavioral reference, but move orchestration into C and keep external tool usage isolated behind small helper functions.

**Tech Stack:** C, `ncurses`, static linking, Makefile, shell build integration, existing AdavaLinux installer assets

---

### Task 1: Capture the current installer behavior to preserve

**Files:**
- Reference: `filesforlinux/rootfs/root/install.sh`
- Reference: `README.md`
- Reference: `build.sh`

**Step 1: List the legacy installer phases**

Document the shell flow in working notes:

- environment preparation
- installer media detection
- target disk validation
- partitioning and formatting
- rootfs extraction
- account and hostname setup
- `fstab` and bootloader configuration
- cleanup and completion

**Step 2: List external commands that the C installer must drive**

Identify the commands used by the shell script, such as:

- `mount`
- `umount`
- `mkfs.ext2`
- `fsck.ext2`
- `blkid`
- `gzip`
- `cpio`
- `grub-install`

### Task 2: Scaffold the installer source tree

**Files:**
- Create: `tools/installer/Makefile`
- Create: `tools/installer/main.c`
- Create: `tools/installer/installer.h`
- Create: `tools/installer/ui.h`
- Create: `tools/installer/ui.c`
- Create: `tools/installer/state.h`
- Create: `tools/installer/sys.h`
- Create: `tools/installer/sys.c`

**Step 1: Write the failing build target**

Add a `Makefile` that expects source files for a static `installer` binary and intentionally references missing symbols so the first build fails with unresolved implementation.

**Step 2: Run the installer build to verify it fails**

Run: `make -C tools/installer`

Expected: FAIL because the program skeleton is incomplete.

**Step 3: Add minimal source skeleton**

Create:

- `main.c` with `main()`
- shared headers for UI and state
- `ui.c` and `sys.c` with stub functions

**Step 4: Run the installer build to verify it passes**

Run: `make -C tools/installer`

Expected: PASS and produce `tools/installer/installer`

### Task 3: Add shared installer state and screen flow

**Files:**
- Modify: `tools/installer/main.c`
- Modify: `tools/installer/installer.h`
- Modify: `tools/installer/state.h`
- Test: `tools/installer/tests/test_state.c`

**Step 1: Write the failing state test**

Create a small C test that verifies:

- default boot mode is unknown
- wizard starts on welcome screen
- navigation advances and rewinds predictably

**Step 2: Run the state test to verify it fails**

Run: `make -C tools/installer test-state`

Expected: FAIL because the state helpers do not exist yet.

**Step 3: Write the minimal state implementation**

Add:

- screen enum
- installer context struct
- simple next/back screen transition helpers

**Step 4: Run the state test to verify it passes**

Run: `make -C tools/installer test-state`

Expected: PASS

### Task 4: Build the base `menuconfig`-style UI layer

**Files:**
- Modify: `tools/installer/ui.h`
- Modify: `tools/installer/ui.c`
- Test: `tools/installer/tests/test_progress.c`

**Step 1: Write the failing progress formatting test**

Add a test for a pure helper that renders progress text, for example:

```c
assert_string_equal(format_progress_bar(50, 10), "[#####     ] 50%");
```

**Step 2: Run the test to verify it fails**

Run: `make -C tools/installer test-progress`

Expected: FAIL because the formatter is not implemented.

**Step 3: Write minimal rendering helpers**

Implement helpers for:

- title bar text
- footer action hints
- centered dialog geometry
- progress bar string formatting

Keep terminal drawing itself in `ui.c`.

**Step 4: Run the test to verify it passes**

Run: `make -C tools/installer test-progress`

Expected: PASS

### Task 5: Implement the welcome and disk selection screens

**Files:**
- Modify: `tools/installer/main.c`
- Modify: `tools/installer/ui.c`
- Modify: `tools/installer/sys.h`
- Modify: `tools/installer/sys.c`

**Step 1: Write the failing disk enumeration test**

Add a pure parser or adapter test that verifies block device metadata is converted into UI list items correctly.

**Step 2: Run the test to verify it fails**

Run: `make -C tools/installer test-disk`

Expected: FAIL because the parser or mapping function is missing.

**Step 3: Write minimal disk discovery support**

Implement:

- boot mode detection
- block device scan from `/sys/block`
- filtering out loop, ram, and optical devices
- size formatting for display

**Step 4: Add the first interactive screens**

Wire the welcome screen and target disk selection into the main loop.

**Step 5: Run the focused tests**

Run: `make -C tools/installer test-disk`

Expected: PASS

### Task 6: Implement identity entry and summary screens

**Files:**
- Modify: `tools/installer/main.c`
- Modify: `tools/installer/ui.c`
- Modify: `tools/installer/state.h`
- Test: `tools/installer/tests/test_validation.c`

**Step 1: Write the failing validation test**

Add tests for:

- valid username acceptance
- rejecting `root`
- rejecting uppercase and invalid characters
- rejecting empty passwords

**Step 2: Run the validation test to verify it fails**

Run: `make -C tools/installer test-validation`

Expected: FAIL because validation helpers are not implemented.

**Step 3: Write minimal validation and form support**

Implement:

- username validation
- password confirmation checks
- field editing for hostname, username, and passwords
- summary screen rendering

**Step 4: Run the validation test to verify it passes**

Run: `make -C tools/installer test-validation`

Expected: PASS

### Task 7: Port the installer execution helpers from shell orchestration to C

**Files:**
- Modify: `tools/installer/sys.h`
- Modify: `tools/installer/sys.c`
- Create: `tools/installer/install.h`
- Create: `tools/installer/install.c`
- Reference: `filesforlinux/rootfs/root/install.sh`

**Step 1: Write the failing helper test**

Add focused tests for pure helpers such as:

- partition path generation for `sdX`, `nvme`, and `mmcblk`
- username and path formatting
- progress step labeling

**Step 2: Run the helper test to verify it fails**

Run: `make -C tools/installer test-install`

Expected: FAIL because the helper logic is still missing.

**Step 3: Write the minimal install helper layer**

Implement C helpers for:

- safe unmount
- mount status lookup
- partition device naming
- UUID lookup wrapper
- subprocess execution with captured log lines

**Step 4: Run the helper test to verify it passes**

Run: `make -C tools/installer test-install`

Expected: PASS

### Task 8: Implement the installation progress screen and step runner

**Files:**
- Modify: `tools/installer/main.c`
- Modify: `tools/installer/ui.c`
- Modify: `tools/installer/install.c`

**Step 1: Write the failing progress runner test**

Add a test around a pure step sequencing helper that verifies:

- step count is correct
- percent values advance monotonically
- failed step stops execution

**Step 2: Run the test to verify it fails**

Run: `make -C tools/installer test-runner`

Expected: FAIL because the runner does not exist.

**Step 3: Write the minimal runner**

Implement:

- ordered install step table
- current step label updates
- progress percentage calculation
- log line appending
- early stop on command failure

**Step 4: Run the test to verify it passes**

Run: `make -C tools/installer test-runner`

Expected: PASS

### Task 9: Integrate the installer into `build.sh`

**Files:**
- Modify: `build.sh`
- Reference: `tools/installer/Makefile`

**Step 1: Write the failing integration check**

Define the intended build expectation:

- `build.sh` invokes `make -C tools/installer`
- build aborts clearly if static build fails
- resulting binary is copied into rootfs staging

**Step 2: Run a focused build segment to verify it fails before integration**

Run: `bash -n build.sh`

Expected: PASS syntax check, but no installer build invocation yet.

**Step 3: Add the installer build and copy flow**

Modify `build.sh` to:

- build `tools/installer/installer`
- copy it to `filesforlinux/rootfs/usr/bin/installer`
- ensure staging rootfs picks it up for the installer image

**Step 4: Run the build syntax check**

Run: `bash -n build.sh`

Expected: PASS

### Task 10: Replace or retire the legacy shell entrypoint carefully

**Files:**
- Modify: `filesforlinux/rootfs/root/install.sh`
- Modify: `README.md`

**Step 1: Decide the compatibility behavior**

Pick one of:

- leave a wrapper script that executes `/usr/bin/installer`
- keep the legacy script temporarily with a deprecation note

**Step 2: Implement the smallest safe compatibility path**

Prefer a wrapper script so documented calls still work during transition.

**Step 3: Update README installer invocation**

Adjust docs to point to the new binary path or wrapper behavior.

### Task 11: Verify the build artifacts before claiming success

**Files:**
- Modify: `build.sh`
- Modify: `tools/installer/*`
- Modify: `filesforlinux/rootfs/usr/bin/installer`

**Step 1: Run focused installer tests**

Run:

- `make -C tools/installer test-state`
- `make -C tools/installer test-progress`
- `make -C tools/installer test-disk`
- `make -C tools/installer test-validation`
- `make -C tools/installer test-install`
- `make -C tools/installer test-runner`

Expected: PASS

**Step 2: Run the installer build**

Run: `make -C tools/installer clean all`

Expected: PASS and produce a statically linked installer binary.

**Step 3: Run the top-level build syntax check**

Run: `bash -n build.sh`

Expected: PASS

**Step 4: Run a full build when dependencies are available**

Run: `./build.sh`

Expected: PASS and the installer binary is present in the built rootfs or installer image.