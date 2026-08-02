#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
desktop_dir="$project_dir/desktop"

require_file() {
  test -f "$1" || {
    printf 'missing required file: %s\n' "$1" >&2
    exit 1
  }
}

require_file "$desktop_dir/Makefile"
require_file "$desktop_dir/adava-panel.c"
require_file "$desktop_dir/adava-greeter.c"
require_file "$desktop_dir/adava-display-manager"
require_file "$desktop_dir/pam_compat.h"
require_file "$desktop_dir/adava-session"
require_file "$desktop_dir/themes/default/theme.conf"
require_file "$desktop_dir/packages/adavalinux-desktop/syspckg-info"
require_file "$desktop_dir/packages/adavalinux-desktop/syspckg-deps"
require_file "$desktop_dir/packages/adavalinux-desktop/install.sh"
grep -qx "entry='tty1::respawn:/usr/bin/adava-display-manager'" "$desktop_dir/packages/adavalinux-desktop/install.sh"
require_file "$desktop_dir/packages/adava-theme-default/syspckg-info"

grep -qx 'PKG_VERSION=0.1.0' "$desktop_dir/packages/adavalinux-desktop/syspckg-info"
grep -qx 'DEP=adava-theme-default-0.1.0' "$desktop_dir/packages/adavalinux-desktop/syspckg-deps"
grep -qx 'DEP=xorg-server-21.1.12' "$desktop_dir/packages/adavalinux-desktop/syspckg-deps"
grep -qx 'DEP=openbox-3.6.1' "$desktop_dir/packages/adavalinux-desktop/syspckg-deps"

make -C "$desktop_dir" clean all

test -x "$desktop_dir/build/usr/bin/adava-panel"
test -x "$desktop_dir/build/usr/bin/adava-session"
test -x "$desktop_dir/build/usr/bin/adava-greeter"
test -x "$desktop_dir/build/usr/bin/adava-display-manager"
test -f "$desktop_dir/build/etc/pam.d/adava-greeter"
test -f "$desktop_dir/build/usr/share/adava/themes/default/theme.conf"

make -C "$desktop_dir" package
tar -tf "$desktop_dir/out/adavalinux-desktop-0.1.0.syspckg" | grep -qx 'adavalinux-desktop-0.1.0/etc/pam.d/adava-greeter'
tar -tf "$desktop_dir/out/adavalinux-desktop-0.1.0.syspckg" | grep -qx 'adavalinux-desktop-0.1.0/install.sh'

printf 'adavalinux desktop contract tests passed\n'
