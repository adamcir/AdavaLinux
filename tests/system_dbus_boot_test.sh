#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rcs="$root/filesforlinux/rootfs/etc/init.d/rcS"

grep -Fqx 'if [ -x /usr/bin/dbus-daemon ]; then' "$rcs"
grep -Fqx '  mkdir -p /run/dbus' "$rcs"
grep -Fqx '  chown messagebus:messagebus /run/dbus 2>/dev/null || true' "$rcs"
grep -Fqx '  if [ ! -S /run/dbus/system_bus_socket ]; then' "$rcs"
grep -Fqx '    /usr/bin/dbus-daemon --system >/dev/null 2>&1 || true' "$rcs"

printf '%s\n' 'system D-Bus boot test passed'
