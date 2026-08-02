#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
pkgdir="$workdir/pkg/e2fsprogs-1.47.3"
mkdir -p "$fake_root/etc" "$workdir/bin" "$workdir/repo/1.0/packages" \
  "$pkgdir/sbin"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF

cat > "$pkgdir/syspckg-info" <<'EOF'
PKG_VERSION=1.47.3
ID=AdavaLinux
VERSION=1.0
EOF
printf '#!/bin/sh\n' > "$pkgdir/sbin/mkfs.ext4"
chmod +x "$pkgdir/sbin/mkfs.ext4"
( cd "$workdir/pkg" && tar -cJf "$workdir/repo/1.0/packages/e2fsprogs-1.47.3.syspckg" e2fsprogs-1.47.3 )

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >> "$TEST_WGET_LOG"
if [ "${1:-}" = "-qO-" ]; then
  printf '<a href="e2fsprogs-1.47.3.syspckg">e2fsprogs-1.47.3.syspckg</a>\n'
  exit 0
fi
if [ "${1:-}" = "-O" ]; then
  case "${3:-}" in
    */1.0/packages/e2fsprogs-1.47.3.syspckg)
      cp "$TEST_REPO/1.0/packages/e2fsprogs-1.47.3.syspckg" "$2"
      exit 0
      ;;
    */1.0/packages/e2fsprogs.syspckg)
      printf 'wrong unversioned URL\n' >&2
      exit 1
      ;;
  esac
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
  "$workdir/syspckg" install e2fsprogs --root "$fake_root" --allow-root -y >/dev/null

grep -q -- '-qO- http://example.invalid/products/AdavaLinux/1.0/packages/' "$workdir/wget.log"
grep -q -- '-O .*e2fsprogs-1.47.3.syspckg' "$workdir/wget.log"
! grep -q -- 'e2fsprogs.syspckg' "$workdir/wget.log"
test -x "$fake_root/sbin/mkfs.ext4"
test -f "$fake_root/var/lib/syspckg/installed/e2fsprogs-1.47.3.list"

printf 'numeric package name install resolution tests passed\n'
