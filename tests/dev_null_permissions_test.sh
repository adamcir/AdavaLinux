#!/bin/sh
set -eu

init=filesforlinux/rootfs/init

grep -Fqx '[ -c /dev/null ] || rm -f /dev/null' "$init"
grep -Fqx '[ -c /dev/null ] || mknod -m 666 /dev/null c 1 3' "$init"
grep -Fqx 'chown root:root /dev/null' "$init"
grep -Fqx 'chmod 666 /dev/null' "$init"

printf '%s\n' '/dev/null boot initialization test passed'
