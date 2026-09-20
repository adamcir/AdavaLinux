#!/bin/sh
set -eu

bin=${1:-./syspckg}
workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT HUP INT TERM

root="$workdir/root"
pkgroot="$workdir/pkgroot"
archive="$workdir/broken-1.0.syspckg"
mkdir -p "$root/etc" "$root/usr/share/syspckg/packages" "$pkgroot/broken-1.0/usr/bin"

cat > "$root/etc/os-release" <<'EOF'
ID=adavalinux
VERSION=1.0
EOF
cat > "$pkgroot/broken-1.0/syspckg-info" <<'EOF'
PKG_VERSION=1.0
ID=adavalinux
VERSION=1.0
EOF
cat > "$pkgroot/broken-1.0/syspckg-deps" <<'EOF'
DEP=required-library-1.0
EOF
printf '%s\n' '#!/bin/sh' 'exit 0' > "$pkgroot/broken-1.0/usr/bin/broken"
chmod +x "$pkgroot/broken-1.0/usr/bin/broken"
( cd "$pkgroot" && tar -cJf "$archive" broken-1.0 )

if "$bin" install "$archive" --root "$root" --allow-root -local -y >"$workdir/out" 2>&1; then
    printf '%s\n' 'expected install with a missing required dependency to fail' >&2
    exit 1
fi

grep -Fq 'Missing required dependencies:' "$workdir/out"
test ! -e "$root/usr/bin/broken"

printf '%s\n' 'missing dependency failure test passed'
