#!/bin/sh
set -eu
cd "$(dirname "$0")/.."

./syspckg --version | grep -qx '1.0'
./syspckg --help >/dev/null 2>&1

./syspckg > /tmp/syspckg-usage.out 2>&1
grep -q 'Usage:' /tmp/syspckg-usage.out

if ./syspckg mc > /tmp/syspckg-short.out 2>&1; then
    echo "ERR: short package syntax unexpectedly succeeded" >&2
    exit 1
fi
grep -q "Unknown command 'mc'" /tmp/syspckg-short.out
grep -q 'syspckg install mc' /tmp/syspckg-short.out

rm -f /tmp/syspckg-usage.out /tmp/syspckg-short.out
printf '%s\n' "syspckg CLI smoke tests passed"
