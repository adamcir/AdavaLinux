#!/bin/sh
set -eu

source=../syspckg/adaterm-0.1.0/adaterm.c
metadata=../syspckg/adaterm-0.1.0/syspckg-info
desktop_deps=../adavalinux-desktop/packages/adavalinux-desktop/syspckg-deps

grep -Fq 'ConfigureNotify' "$source"
grep -Fq 'ButtonPressMask' "$source"
grep -Fq 'XK_Page_Up' "$source"
grep -Fq 'setenv("TERM", "linux"' "$source"
grep -Fq 'setenv("LC_ALL", "C"' "$source"
grep -Fq 'PKG_VERSION=0.1.0' "$metadata"
grep -Fq 'DEP=adaterm-0.1.0' "$desktop_deps"
! grep -Fq 'DEP=lxterminal-0.4.0' "$desktop_deps"
! grep -Fq 'DEP=vte-0.28.2' "$desktop_deps"

printf '%s\n' 'AdaTerm package configuration test passed'
