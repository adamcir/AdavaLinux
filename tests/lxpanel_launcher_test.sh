#!/bin/sh
set -eu

installer=../adavalinux-desktop/packages/adavalinux-desktop/install.sh

# Keep the full LXPanel layout but use direct launcher actions: the minimal
# image does not provide a complete desktop-entry launcher backend.
! grep -Fq '    system {' "$installer"
grep -Fq '      name=Menu' "$installer"
grep -Fq '      action=/usr/bin/adaterm' "$installer"
grep -Fq '      action=pcmanfm' "$installer"
grep -Fq '  type = taskbar' "$installer"
grep -Fq '  type = tray' "$installer"
grep -Fq '  type = dclock' "$installer"
grep -Fq '  background=0' "$installer"
! grep -Fq 'backgroundfile=/usr/share/lxpanel/images/background.png' "$installer"

printf '%s\n' 'LXPanel direct launcher configuration test passed'
