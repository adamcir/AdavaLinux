#!/bin/sh
set -eu

installer=../adavalinux-desktop/packages/adavalinux-desktop/install.sh

! grep -Fq 'adavalinux-terminal' "$installer"
! grep -Fq 'lxterminal.desktop' "$installer"

printf '%s\n' 'legacy LXTerminal references removed from desktop test passed'
