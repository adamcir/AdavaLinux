#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
mkdir -p "$fake_root/etc" "$workdir/bin"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

printf '%s\n' "$*" >> "$TEST_WGET_LOG"
if [ "${1:-}" = "-qO-" ]; then
  case "${2:-}" in
    http://example.invalid/products/AdavaLinux/)
      printf '<a href="1.0/">1.0/</a>\n'
      printf '<a href="1.1/">1.1/</a>\n'
      printf '<a href="kernel/">kernel/</a>\n'
      ;;
    http://example.invalid/products/AdavaLinux/kernel/)
      printf '<a href="kernel-6.19.11.syspckg">kernel-6.19.11.syspckg</a>\n'
      printf '<a href="kernel-6.19.12.syspckg">kernel-6.19.12.syspckg</a>\n'
      ;;
    http://example.invalid/products/AdavaLinux/1.0/packager/)
      printf '<a href="syspckg-1.0.syspckg">syspckg-1.0.syspckg</a>\n'
      printf '<a href="syspckg-1.0.1.syspckg">syspckg-1.0.1.syspckg</a>\n'
      ;;
  esac
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

run_update() {
  local log_file="$1"
  shift
  : > "$log_file"
  PATH="$workdir/bin:$PATH" TEST_WGET_LOG="$log_file" \
    "$workdir/syspckg" update "$@" --dry-run --root "$fake_root" --allow-root
}

system_log="$workdir/system.log"
system_out="$(run_update "$system_log" --system)"
grep -q -- '-qO- http://example.invalid/products/AdavaLinux/' "$system_log"
grep -q 'http://example.invalid/products/AdavaLinux/1.1/adavalinux-1.1.syspckg' <<<"$system_out"

kernel_log="$workdir/kernel.log"
kernel_out="$(run_update "$kernel_log" --kernel)"
grep -q -- '-qO- http://example.invalid/products/AdavaLinux/kernel/' "$kernel_log"
grep -q 'http://example.invalid/products/AdavaLinux/kernel/kernel-6.19.12.syspckg' <<<"$kernel_out"

packager_log="$workdir/packager.log"
packager_out="$(run_update "$packager_log" --packager)"
grep -q -- '-qO- http://example.invalid/products/AdavaLinux/1.0/packager/' "$packager_log"
grep -q 'http://example.invalid/products/AdavaLinux/1.0/packager/syspckg-1.0.1.syspckg' <<<"$packager_out"

printf 'update layout tests passed\n'
