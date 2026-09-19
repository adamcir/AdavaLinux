#!/bin/sh
set -eu

cd "$(dirname "$0")/.."
make clean all >/dev/null

run() {
    SYSPCKG2_BACKEND=/bin/echo ./syspckg2 "$@" 2>/dev/null
}

[ "$(run xfce4)" = "install xfce4" ]
[ "$(run install adaterm --adava -y)" = "install --from-repo=adavalinux adaterm -y" ]
[ "$(run install xfce4 --fedora)" = "install --from-repo=fedora,updates xfce4" ]
[ "$(run search terminal --fedora)" = "--repo=fedora,updates search terminal" ]
[ "$(run repos)" = "repo list" ]
[ "$(run clean)" = "clean all" ]

printf '%s\n' "syspckg2 CLI tests passed"
