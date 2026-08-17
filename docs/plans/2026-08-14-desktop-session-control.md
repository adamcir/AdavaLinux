# Desktop Session Control Implementation Plan

> **For Codex:** Execute with tests before each behavioral change.

**Goal:** Keep Xorg alive across XFCE logout, support normal adduser accounts, and provide root-password-authorized administrative actions.

**Architecture:** The display manager owns Xorg for its entire lifetime and repeatedly runs the login UI.  A successful login runs XFCE as the authenticated account; when it exits, the UI returns.  Administrative actions use sudo configured to authenticate the root password.

**Tech Stack:** BusyBox init, GTK3, PAM, XFCE, sudo.

---

### Task 1: Preserve Xorg across login UI exits

**Files:** `../adavalinux-desktop/adavalinux-display-manager`, desktop lifecycle test.

Write a failing test requiring a login loop, then ensure the manager only terminates Xorg during manager shutdown.

### Task 2: Support accounts created by adduser

**Files:** `../adavalinux-desktop/adavalinux-logon.c`, logon test.

Write a failing test requiring the account home, primary group and supplementary groups to be selected after successful PAM authentication.

### Task 3: Add root-password sudo and power actions

**Files:** syspckg sudo package metadata/configuration, desktop dependencies and launchers.

Add tests for `Defaults rootpw`, PAM configuration and explicit reboot/poweroff commands; package only their required runtime files.
