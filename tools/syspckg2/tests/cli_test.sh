#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

./syspckg2 --version | grep -qx '0.2.0'
./syspckg2 --help >/dev/null 2>&1

./syspckg2 > /tmp/syspckg2-usage.out 2>&1
grep -q 'Usage:' /tmp/syspckg2-usage.out

if ./syspckg2 mc > /tmp/syspckg2-short.out 2>&1; then
    echo "ERR: short package syntax unexpectedly succeeded" >&2
    exit 1
fi
grep -q "Unknown command 'mc'" /tmp/syspckg2-short.out
grep -q 'syspckg2 install mc' /tmp/syspckg2-short.out

rm -f /tmp/syspckg2-usage.out /tmp/syspckg2-short.out
printf '%s\n' "syspckg2 CLI smoke tests passed"
