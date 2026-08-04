# Full-Disk Root Partition Design

**Goal:** Make an empty root-partition-size field install Linux into all remaining disk space.

**Design:** The partition-script helper will translate an empty size into fdisk's `+0` end-of-disk value. BIOS will therefore create one bootable Linux partition spanning the disk after its initial alignment gap. UEFI will retain its 512 MiB EFI System Partition and allocate the Linux root partition from its fixed start sector through the end of the disk.

**User experience:** The size prompt and installation summary will state that an empty value uses all remaining space. A user can enter an fdisk size expression such as `+10G` to create a smaller Linux partition.

**Testing:** Helper tests will cover the empty input for both BIOS and UEFI partition scripts, alongside the existing explicit-size coverage.
