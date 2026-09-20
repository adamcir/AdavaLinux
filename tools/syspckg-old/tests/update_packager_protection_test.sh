#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
mkdir -p "$fake_root/etc" "$fake_root/usr/share/syspckg" \
  "$fake_root/var/lib/syspckg/installed" "$workdir/bin"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF
printf '1.0\n' > "$fake_root/usr/share/syspckg/packager-version"
printf 'f /usr/bin/syspckg\n' > "$fake_root/var/lib/syspckg/installed/syspckg-1.0.list"

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "-qO-" ]; then
  printf '<a href="syspckg-1.0.syspckg">syspckg-1.0.syspckg</a>\n'
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

if "$workdir/syspckg" remove syspckg --root "$fake_root" --allow-root >"$workdir/remove.out" 2>&1; then
  printf 'remove syspckg unexpectedly succeeded\n' >&2
  exit 1
fi
grep -q 'Cannot remove SystemPackager' "$workdir/remove.out"
test -f "$fake_root/var/lib/syspckg/installed/syspckg-1.0.list"

packager_out="$(PATH="$workdir/bin:$PATH" \
  "$workdir/syspckg" update --packager --dry-run --root "$fake_root" --allow-root)"

grep -q 'Packager is up to date: 1.0' <<<"$packager_out"
! grep -q 'Would install:' <<<"$packager_out"

printf 'packager protection tests passed\n'
