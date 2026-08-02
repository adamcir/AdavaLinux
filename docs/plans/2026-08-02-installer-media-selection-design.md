# Installer Media Selection Design

**Goal:** Add an installer wizard step that lets the user choose the installation media before choosing the target disk.

**Design:** Insert `STEP_MEDIA` between the welcome and target disk steps in `tools/installer/ui.c`. The step lists supported block devices, stores the selected path in `InstallerConfig`, and the target disk step excludes that selected media path.

**Behavior:**
- Installation media candidates include whole block devices that can plausibly hold the installer, including optical media such as `/dev/sr0` and regular whole disks such as `/dev/sda`, `/dev/vda`, `/dev/xvda`, `/dev/nvme0n1`, and `/dev/mmcblk0`.
- Target disks keep their existing stricter filtering and do not include the selected installation media.
- Back navigation from target disk returns to media selection.

**Testing:** Add helper-level tests for media device filtering and target disk exclusion, then run `make -C tools/installer test`.

