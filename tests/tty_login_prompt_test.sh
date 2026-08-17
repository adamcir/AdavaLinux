#!/bin/sh
set -eu

issue=filesforlinux/rootfs/etc/issue
inittab=filesforlinux/rootfs/etc/inittab

test -f "$issue"
test "$(cat "$issue")" = '\n \l '
grep -Fqx "printf '\\n \\l ' > /etc/issue" filesforlinux/rootfs/etc/init.d/rcS

for tty in tty1 tty2 tty3 tty4 tty5 tty6 tty7 ttyS0; do
  grep -Fq "getty 115200 $tty" "$inittab"
done

! grep -Fq 'getty -i' "$inittab"

printf '%s\n' 'TTY login prompt test passed'
