#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
mkdir -p "$fake_root/etc" "$fake_root/boot" "$workdir/bin"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF
printf 'same kernel\n' > "$fake_root/boot/vmlinuz-6.19.12"

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >> "$TEST_WGET_LOG"
if [ "${1:-}" = "-qO-" ]; then
  case "${2:-}" in
    http://example.invalid/products/AdavaLinux/kernel/)
      printf '<a href="kernel-6.19.12.syspckg">kernel-6.19.12.syspckg</a>\n'
      ;;
  esac
  exit 0
fi
if [ "${1:-}" = "-O" ]; then
  exit 42
fi
exit 1
EOF
chmod +x "$workdir/bin/wget"

cat > "$workdir/syspckg-source" <<'EOF'
SOURCE_URL=http://example.invalid/products/AdavaLinux
EOF

gcc -D_GNU_SOURCE -std=c11 -Wall -Wextra -DSYSPCKG_SOURCE_FILE='"'"$workdir/syspckg-source"'"' \
  main.c -o "$workdir/syspckg"

log_file="$workdir/wget.log"
: > "$log_file"
out="$(PATH="$workdir/bin:$PATH" TEST_WGET_LOG="$log_file" \
  "$workdir/syspckg" update --kernel --root "$fake_root" --allow-root -y)"

grep -q 'Kernel is up to date: 6.19.12' <<<"$out"
grep -q -- '-qO- http://example.invalid/products/AdavaLinux/kernel/' "$log_file"
! grep -q -- '-O ' "$log_file"

printf 'update kernel same version tests passed\n'
