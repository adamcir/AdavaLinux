#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

grep -Fqx '/usr/lib' "$root/filesforlinux/rootfs/etc/ld.so.conf"
grep -Fqx '/usr/lib/x86_64-linux-gnu' "$root/filesforlinux/rootfs/etc/ld.so.conf"
grep -Fq 'refresh_dynamic_linker_cache(root)' "$root/tools/syspckg/main.c"
grep -Fq '"/sbin/ldconfig"' "$root/tools/syspckg/main.c"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/etc" "$stage/usr/lib/x86_64-linux-gnu"
cp "$root/filesforlinux/rootfs/etc/ld.so.conf" "$stage/etc/ld.so.conf"
cp -a "$root/../syspckg/libs/dbus-1.14.10/usr/lib/libdbus-1.so.3"* "$stage/usr/lib/"
cp -a "$root/../syspckg/libs/systemd-256.7/usr/lib/x86_64-linux-gnu/libsystemd.so.0"* \
  "$stage/usr/lib/x86_64-linux-gnu/"
cp -a "$root/../syspckg/libs/expat-2.7.4/usr/lib/libexpat.so.1"* "$stage/usr/lib/"
cp -a "$root/../syspckg/libs/libselinux-3.7/usr/lib/libselinux.so.1" "$stage/usr/lib/"
cp -a "$root/../syspckg/libs/libcap-2.77/usr/lib/libcap.so.2"* "$stage/usr/lib/"
cp -a "$root/../syspckg/libs/pcre2-10.47/usr/lib/libpcre2-8.so.0"* "$stage/usr/lib/"
/sbin/ldconfig -r "$stage"
/sbin/ldconfig -r "$stage" -p | grep -Fq 'libsystemd.so.0'
/sbin/ldconfig -r "$stage" -p | grep -Fq 'libcap.so.2'
/sbin/ldconfig -r "$stage" -p | grep -Fq 'libdbus-1.so.3'

# Exercise the actual package-manager transaction against a clean target.
# Its package hook and the final syspckg ldconfig refresh must make the whole
# D-Bus closure visible without a hand-written library symlink.
transaction_root=$(mktemp -d)
trap 'rm -rf "$stage" "$transaction_root"' EXIT
mkdir -p "$transaction_root/etc" "$transaction_root/sbin"
cp "$root/filesforlinux/rootfs/etc/os-release" "$transaction_root/etc/os-release"
cp "$root/filesforlinux/rootfs/etc/ld.so.conf" "$transaction_root/etc/ld.so.conf"
cp /sbin/ldconfig "$transaction_root/sbin/ldconfig"
(
  cd "$root/../syspckg/packages"
  "$root/tools/syspckg/syspckg" install "$PWD/dbus-1.14.10.syspckg" \
    --root "$transaction_root" -local -y >/dev/null
)
/sbin/ldconfig -r "$transaction_root" -p | grep -Fq 'libsystemd.so.0'
/sbin/ldconfig -r "$transaction_root" -p | grep -Fq 'libcap.so.2'
/sbin/ldconfig -r "$transaction_root" -p | grep -Fq 'libdbus-1.so.3'

printf '%s\n' 'Runtime linker cache integration test passed'
