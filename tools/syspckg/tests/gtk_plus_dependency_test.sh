#!/bin/sh
set -eu

bin=${1:-./syspckg}
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

root="$workdir/root"
pkgroot="$workdir/pkgroot"
mkdir -p "$root/etc" "$root/usr/share/syspckg/packages" \
         "$pkgroot/lxde-piece-1.0/usr/bin" "$pkgroot/gtk+-2.24.33/usr/lib"

cat > "$root/etc/os-release" <<'EOF'
ID=adavalinux
VERSION=1.0
EOF
cat > "$pkgroot/lxde-piece-1.0/syspckg-info" <<'EOF'
PKG_VERSION=1.0
ID=adavalinux
VERSION=1.0
EOF
cat > "$pkgroot/lxde-piece-1.0/syspckg-deps" <<'EOF'
DEP=gtk+-2.24.33
EOF
cat > "$pkgroot/gtk+-2.24.33/syspckg-info" <<'EOF'
PKG_VERSION=2.24.33
ID=adavalinux
VERSION=1.0
EOF
printf '%s\n' '#!/bin/sh' 'exit 0' > "$pkgroot/lxde-piece-1.0/usr/bin/lxde-piece"
printf '%s\n' 'gtk runtime' > "$pkgroot/gtk+-2.24.33/usr/lib/libgtk-x11-2.0.so.0"
chmod +x "$pkgroot/lxde-piece-1.0/usr/bin/lxde-piece"
( cd "$pkgroot" && tar -cJf "$root/usr/share/syspckg/packages/lxde-piece-1.0.syspckg" lxde-piece-1.0 )
( cd "$pkgroot" && tar -cJf "$root/usr/share/syspckg/packages/gtk+-2.24.33.syspckg" gtk+-2.24.33 )

"$bin" install lxde-piece-1.0 --root "$root" --allow-root -local -y >"$workdir/out" 2>&1

test -x "$root/usr/bin/lxde-piece"
test -f "$root/usr/lib/libgtk-x11-2.0.so.0"
test -f "$root/var/lib/syspckg/installed/gtk+-2.24.33.list"

printf '%s\n' 'gtk+ dependency test passed'
