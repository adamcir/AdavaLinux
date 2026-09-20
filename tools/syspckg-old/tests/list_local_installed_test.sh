#!/usr/bin/env bash
set -euo pipefail

workdir="$(mktemp -d)"
trap 'rm -rf "$workdir"' EXIT

fake_root="$workdir/root"
mkdir -p "$fake_root/etc" "$fake_root/var/lib/syspckg/installed" "$workdir/bin"
cat > "$fake_root/etc/os-release" <<'EOF'
ID=AdavaLinux
VERSION=1.0
EOF
printf 'f /usr/bin/demo\n' > "$fake_root/var/lib/syspckg/installed/demo-1.0.list"
printf 'f /usr/bin/e2fsck\n' > "$fake_root/var/lib/syspckg/installed/e2fsprogs-1.47.3.list"

cat > "$workdir/bin/wget" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
printf 'list --local must not use wget\n' >&2
exit 1
EOF
chmod +x "$workdir/bin/wget"

gcc -D_GNU_SOURCE -std=c11 -Wall -Wextra main.c -o "$workdir/syspckg"

out="$(PATH="$workdir/bin:$PATH" "$workdir/syspckg" list --local --root "$fake_root" --allow-root)"
grep -q '^demo-1.0$' <<<"$out"
grep -q '^e2fsprogs-1.47.3$' <<<"$out"
! grep -q '\.list' <<<"$out"

printf 'local installed list tests passed\n'
