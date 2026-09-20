#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
pkgdir="$workdir/pkg/demo-1.0"
mkdir -p "$fake_root/etc" "$workdir/bin" "$workdir/repo/1.0/packages" "$pkgdir/usr/share/demo"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF

cat > "$pkgdir/syspckg-info" <<'EOF'
PKG_VERSION=1.0
ID=AdavaLinux
VERSION=1.0
EOF
printf 'demo\n' > "$pkgdir/usr/share/demo/value"
( cd "$workdir/pkg" && tar -cJf "$workdir/repo/1.0/packages/demo-1.0.syspckg" demo-1.0 )

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "-qO-" ]; then
  printf '<a href="demo-1.0.syspckg">demo-1.0.syspckg</a>\n'
  exit 0
fi
if [ "${1:-}" = "-O" ]; then
  cp "$TEST_REPO/1.0/packages/demo-1.0.syspckg" "$2"
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

printf 'x\n' | PATH="$workdir/bin:$PATH" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" install demo --root "$fake_root" --allow-root >/dev/null
test ! -f "$fake_root/usr/share/demo/value"
test ! -f "$fake_root/var/lib/syspckg/installed/demo-1.0.list"

printf '\n' | PATH="$workdir/bin:$PATH" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" install demo --root "$fake_root" --allow-root >/dev/null
test -f "$fake_root/usr/share/demo/value"
test -f "$fake_root/var/lib/syspckg/installed/demo-1.0.list"

printf 'x\n' | "$workdir/syspckg" remove demo --root "$fake_root" --allow-root >/dev/null
test -f "$fake_root/usr/share/demo/value"
test -f "$fake_root/var/lib/syspckg/installed/demo-1.0.list"

printf 'y\n' | "$workdir/syspckg" remove demo --root "$fake_root" --allow-root >/dev/null
test ! -f "$fake_root/usr/share/demo/value"
test ! -f "$fake_root/var/lib/syspckg/installed/demo-1.0.list"

printf 'install/remove confirmation tests passed\n'
