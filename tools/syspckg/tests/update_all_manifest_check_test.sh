#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
pkgdir="$workdir/pkg/demo-1.1"
mkdir -p "$fake_root/etc" "$fake_root/var/lib/syspckg/installed" "$fake_root/usr/share/demo" \
  "$workdir/bin" "$workdir/repo/1.0/packages" "$pkgdir/usr/share/demo"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF
printf 'f /usr/share/demo\n' > "$fake_root/var/lib/syspckg/installed/demo-1.0.list"
printf 'f /usr/share/unchanged\n' > "$fake_root/var/lib/syspckg/installed/unchanged-1.0.list"
printf 'old\n' > "$fake_root/usr/share/demo/version"

cat > "$pkgdir/syspckg-info" <<'EOF'
PKG_VERSION=1.1
ID=AdavaLinux
VERSION=1.0
EOF
printf 'new\n' > "$pkgdir/usr/share/demo/version"
( cd "$workdir/pkg" && tar -cJf "$workdir/repo/1.0/packages/demo-1.1.syspckg" demo-1.1 )

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >> "$TEST_WGET_LOG"
if [ "${1:-}" = "-qO-" ]; then
  printf '<a href="demo-1.1.syspckg">demo-1.1.syspckg</a>\n'
  printf '<a href="unchanged-1.0.syspckg">unchanged-1.0.syspckg</a>\n'
  printf '<a href="extra-2.0.syspckg">extra-2.0.syspckg</a>\n'
  exit 0
fi
if [ "${1:-}" = "-O" ]; then
  cp "$TEST_REPO/1.0/packages/demo-1.1.syspckg" "$2"
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
dry_out="$(PATH="$workdir/bin:$PATH" TEST_WGET_LOG="$workdir/wget.log" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" update --all --dry-run --root "$fake_root" --allow-root)"

grep -q -- '-qO- http://example.invalid/products/AdavaLinux/1.0/packages/' "$workdir/wget.log"
! grep -q -- '-O ' "$workdir/wget.log"
grep -q 'Update available: demo 1.0 -> 1.1' <<<"$dry_out"
! grep -q 'unchanged' <<<"$dry_out"
! grep -q 'extra' <<<"$dry_out"

: > "$workdir/wget.log"
PATH="$workdir/bin:$PATH" TEST_WGET_LOG="$workdir/wget.log" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" update --all --root "$fake_root" --allow-root -y >/dev/null

grep -q -- '-O ' "$workdir/wget.log"
grep -q '^new$' "$fake_root/usr/share/demo/version"
test -f "$fake_root/var/lib/syspckg/installed/demo-1.1.list"
test ! -f "$fake_root/var/lib/syspckg/installed/demo-1.0.list"
test -f "$fake_root/var/lib/syspckg/installed/unchanged-1.0.list"

printf 'update-all manifest check tests passed\n'
