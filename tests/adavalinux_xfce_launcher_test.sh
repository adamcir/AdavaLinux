#!/bin/sh
set -eu

launcher=../syspckg/adavalinux-xfce-0.1.0/usr/bin/adavalinux-xfce-start

[ -x "$launcher" ]
grep -Fqx 'chvt 1 2>/dev/null || true' "$launcher"
grep -Fqx 'rm -rf "${XDG_CACHE_HOME:-$HOME/.cache}/sessions"' "$launcher"
grep -Fqx 'exec /usr/bin/xfce4-session' "$launcher"
grep -Fqx 'if pidof Xorg >/dev/null 2>&1; then' "$launcher"
! grep -Fq 'xprop -root' "$launcher"
! grep -Fq '/usr/bin/startxfce4 &' "$launcher"

printf '%s\n' 'adavalinux XFCE launcher test passed'
