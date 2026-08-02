#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
pkgdir="$workdir/pkg/hooked-1.0"
mkdir -p "$fake_root/etc" "$workdir/bin" "$workdir/repo/1.0/packages" "$pkgdir/usr/share/hooked"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF
cat > "$pkgdir/syspckg-info" <<'EOF'
PKG_VERSION=1.0
ID=AdavaLinux
VERSION=1.0
EOF
cat > "$pkgdir/install.sh" <<'EOF'
#!/bin/sh
set -eu
test -f "$1/usr/share/hooked/value"
printf '%s\n' "$2" >> "$1/etc/hooked-action"
EOF
chmod +x "$pkgdir/install.sh"
printf 'hooked\n' > "$pkgdir/usr/share/hooked/value"
( cd "$workdir/pkg" && tar -cJf "$workdir/repo/1.0/packages/hooked-1.0.syspckg" hooked-1.0 )

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [ "${1:-}" = "-qO-" ]; then
  printf '<a href="hooked-1.0.syspckg">hooked-1.0.syspckg</a>\n'
elif [ "${1:-}" = "-O" ]; then
  cp "$TEST_REPO/1.0/packages/hooked-1.0.syspckg" "$2"
else
  exit 1
fi
EOF
chmod +x "$workdir/bin/wget"
printf '%s\n' 'SOURCE_URL=http://example.invalid/products/AdavaLinux' > "$workdir/syspckg-source"

gcc -D_GNU_SOURCE -std=c11 -Wall -Wextra -DSYSPCKG_SOURCE_FILE='"'"$workdir/syspckg-source"'"' \
  main.c -o "$workdir/syspckg"

install_output=$(PATH="$workdir/bin:$PATH" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" install hooked --root "$fake_root" --allow-root -y)

test -f "$fake_root/usr/share/hooked/value"
test "$(cat "$fake_root/etc/hooked-action")" = install
grep -Fx 'Running install.sh' <<<"$install_output"

PATH="$workdir/bin:$PATH" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" install hooked --root "$fake_root" --allow-root -y >/dev/null

test "$(wc -l < "$fake_root/etc/hooked-action")" -eq 2
test "$(tail -n1 "$fake_root/etc/hooked-action")" = install

if PATH="$workdir/bin:$PATH" TEST_REPO="$workdir/repo" \
  "$workdir/syspckg" install hooked -online --root "$fake_root" --allow-root -y >/dev/null 2>&1; then
  printf '%s\n' 'expected -online to be rejected' >&2
  exit 1
fi

printf 'install hook tests passed\n'
