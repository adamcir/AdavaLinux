#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

./syspckg2 --version | grep -qx '0.2.0'
./syspckg2 --help >/dev/null

printf '%s\n' "syspckg2 libdnf5 frontend smoke tests passed"
