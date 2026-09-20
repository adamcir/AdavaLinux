#!/usr/bin/env bash
set -euo pipefail

bin=${1:-./syspckg}
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

root="$workdir/root"
pkgroot="$workdir/pkg"
mkdir -p "$root/etc" "$root/usr/share/syspckg/packages" \
  "$pkgroot/alpha-1.0/usr/bin" "$pkgroot/beta-1.0/usr/bin"

cat > "$root/etc/os-release" <<'EOF'
ID=adavalinux
VERSION=1.0
EOF

for pkg in alpha beta; do
  cat > "$pkgroot/$pkg-1.0/syspckg-info" <<EOF
PKG_VERSION=1.0
ID=adavalinux
VERSION=1.0
EOF
  : > "$pkgroot/$pkg-1.0/syspckg-deps"
  printf '#!/bin/sh\nexit 0\n' > "$pkgroot/$pkg-1.0/usr/bin/$pkg"
  chmod +x "$pkgroot/$pkg-1.0/usr/bin/$pkg"
  ( cd "$pkgroot" && tar -cJf "$root/usr/share/syspckg/packages/$pkg-1.0.syspckg" "$pkg-1.0" )
done

"$bin" install alpha beta --root "$root" --allow-root -local -y >"$workdir/out" 2>&1

test -x "$root/usr/bin/alpha"
test -x "$root/usr/bin/beta"
test -f "$root/var/lib/syspckg/installed/alpha-1.0.list"
test -f "$root/var/lib/syspckg/installed/beta-1.0.list"

printf '%s\n' 'multiple package install test passed'
