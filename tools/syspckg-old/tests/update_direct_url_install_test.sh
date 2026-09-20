#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
pkgdir="$workdir/pkg/adavalinux-1.1"
mkdir -p "$fake_root/etc" "$workdir/bin" "$workdir/repo/1.1" "$pkgdir/etc"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF

cat > "$pkgdir/syspckg-info" <<'EOF'
PKG_VERSION=1.1
ID=AdavaLinux
VERSION=1.1
EOF
cat > "$pkgdir/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.1
EOF
( cd "$workdir/pkg" && tar -cJf "$workdir/repo/1.1/adavalinux-1.1.syspckg" adavalinux-1.1 )

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >> "$TEST_WGET_LOG"
if [ "${1:-}" = "-qO-" ]; then
  printf '<a href="1.0/">1.0/</a>\n'
  printf '<a href="1.1/">1.1/</a>\n'
  exit 0
fi
if [ "${1:-}" = "-O" ]; then
  cp "$TEST_REPO/1.1/adavalinux-1.1.syspckg" "$2"
  exit 0
fi
exit 1
EOF
chmod +x "$workdir/bin/wget"

cat > "$workdir/syspckg-source" <<'EOF'
SOURCE_URL=http://example.invalid/products/AdavaLinux
EOF

gcc -D_GNU_SOURCE -std=c11 -Wall -Wextra -DSYSPCKG_SOURCE_FILE='"'"$workdir/syspckg-source"'"' \
  main.c -o "$workdir/syspckg"

: > "$workdir/wget.log"
PATH="$workdir/bin:$PATH" TEST_WGET_LOG="$workdir/wget.log" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" update --system --root "$fake_root" --allow-root -y >/dev/null

grep -q 'VERSION=1.1' "$fake_root/etc/os-release"
grep -q -- '-O ' "$workdir/wget.log"

printf 'update direct URL install tests passed\n'
