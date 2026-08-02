#!/bin/sh
set -eu

root=$1
inittab="$root/etc/inittab"
entry='tty1::respawn:/usr/bin/adava-display-manager'

test -f "$inittab"
if grep -q '^tty1::' "$inittab"; then
  sed -i "s|^tty1::.*|$entry|" "$inittab"
else
  printf '%s\n' "$entry" >> "$inittab"
fi
