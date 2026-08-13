#!/bin/sh
set -eu

installer=tools/installer/install.c

# A media device chosen in the UI is authoritative.  Its failure must not
# silently fall back to another device such as /dev/sr0.
grep -Fq 'cfg->install_media[0]' "$installer"
grep -Fq 'Using selected installer media: %s' "$installer"
grep -Fq 'Selected installer media does not contain AdavaLinux files: %s' "$installer"
grep -Fq 'mount_selected_disk_partition(cfg->install_media' "$installer"
grep -Fq 'installer_partition_belongs_to_disk(disk, candidate)' "$installer"
grep -Fq '/sys/class/block/%s' "$installer"
grep -Fq 'installer_scan_install_media(media, 32, &media_count)' "$installer"

printf '%s\n' 'selected installer media test passed'
