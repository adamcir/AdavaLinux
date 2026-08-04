#!/bin/sh
set -eu

init_file=filesforlinux/rootfs/init
desktop_start=../adavalinux-desktop/adavalinux-lxde-start

grep -Fq 'mkdir -p /dev/pts' "$init_file"
grep -Fq 'mount -t devpts -o mode=620,ptmxmode=666 devpts /dev/pts' "$init_file"
grep -Fq 'mount -t devpts -o mode=620,ptmxmode=666 devpts /dev/pts' "$desktop_start"
grep -Fq 'mount -o remount,mode=620,ptmxmode=666 -t devpts devpts /dev/pts' "$desktop_start"
grep -Fq 'ln -sfn pts/ptmx /dev/ptmx' "$init_file"
grep -Fq 'ln -sfn pts/ptmx /dev/ptmx' "$desktop_start"

printf '%s\n' 'devpts mount test passed'
