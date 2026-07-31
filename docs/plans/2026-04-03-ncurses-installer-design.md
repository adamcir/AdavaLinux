# Ncurses Installer Design

**Goal:** Replace the current shell-based installer with a statically linked C installer that uses a classic `menuconfig`-style `ncurses` user interface.

**Scope:** This change adds a new installer implementation under `tools/installer`, integrates its build into `build.sh`, and installs the resulting binary into `filesforlinux/rootfs/usr/sbin/`. It preserves the current sequential installation flow rather than introducing a non-linear UI.

## Chosen Approach

Implement a single statically linked `installer` binary in C with a custom lightweight UI layer on top of `ncurses`. The UI will mimic classic Linux TUI tools: a title bar, framed centered dialogs, highlighted selections, footer action hints, and a dedicated progress screen with a progress bar and live log output.

## UI Structure

- `Welcome`: destructive warning and detected boot mode
- `Target Disk`: selectable disk list with basic metadata
- `Confirmation`: explicit wipe confirmation screen
- `Identity`: hostname, username, user password, and root password entry
- `Summary`: final confirmation before execution
- `Installing`: progress bar, current step label, and scrolling log pane
- `Result`: success or failure summary with next action

## Runtime Model

The installer remains a sequential wizard. Each screen maps to one phase of the old shell script. System changes are performed by the C binary through direct file operations where practical and `fork`/`exec` for external tools such as formatting, mounting, archive extraction, and GRUB installation.

## Build Integration

- Source lives in `tools/installer`
- `build.sh` compiles the installer before packaging the installer initramfs
- The built binary is copied into `filesforlinux/rootfs/usr/sbin/`
- Existing packaging logic is adjusted to include the binary instead of relying on `/root/install.sh`

## Constraints

- Static linking is required
- The UI must remain readable on low-resolution terminals and framebuffer consoles
- The first version should preserve the current partitioning and installation behavior as closely as possible
- Build failures due to missing static `ncurses` artifacts must be explicit and actionable

## Testing Strategy

- Add unit-style coverage for pure helper logic where feasible
- Verify the build produces the installer binary and copies it into rootfs
- Boot the installer ISO in QEMU and walk through an installation on a virtual disk
