#!/bin/sh
set -eu

OUT="${1:?usage: bootstrap-libdnf5-sysroot.sh OUT_DIR [ARCH]}"
ARCH="${2:-amd64}"

APTROOT="$OUT/.apt"
LISTS="$APTROOT/lists"
CACHE="$APTROOT/cache"
ARCHIVES="$CACHE/archives"
STATE="$APTROOT/state"
SOURCES="$APTROOT/sources.list"
STATUS="$STATE/status"
KEYRING="/usr/share/keyrings/debian-archive-keyring.gpg"

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERR: Missing command: $1" >&2
        exit 1
    }
}

need apt-get
need dpkg-deb

runtime_lib_present() {
    pattern="$1"
    find "$OUT/lib/x86_64-linux-gnu" "$OUT/usr/lib/x86_64-linux-gnu" \
        -maxdepth 1 \( -type f -o -type l \) -name "$pattern" -print -quit 2>/dev/null |
        grep -q .
}

sysroot_complete() {
    [ -f "$OUT/usr/include/libdnf5/base/base.hpp" ] || return 1
    runtime_lib_present 'libdnf5.so*' || return 1
    runtime_lib_present 'libc.so.6' || return 1
    runtime_lib_present 'libstdc++.so.6' || return 1
    runtime_lib_present 'libjson-c.so.*' || return 1
    runtime_lib_present 'libsolvext.so.*' || return 1
    runtime_lib_present 'libxml2.so.*' || return 1
    runtime_lib_present 'liblua*.so.*' || return 1
    runtime_lib_present 'libyaml*.so.*' || return 1
    runtime_lib_present 'librpm_sequoia.so.*' || return 1
    runtime_lib_present 'libcurl*.so.*' || return 1
    runtime_lib_present 'libgpgme.so.*' || return 1
}

if sysroot_complete; then
    exit 0
fi

if [ ! -f "$KEYRING" ]; then
    echo "ERR: Missing Debian archive keyring: $KEYRING" >&2
    echo "Install package 'debian-archive-keyring' on the build host." >&2
    exit 1
fi

echo "==> Preparing isolated Debian forky/$ARCH libdnf5 sysroot"
rm -rf "$OUT"
mkdir -p "$OUT" "$LISTS/partial" "$ARCHIVES/partial" "$STATE"
: > "$STATUS"

cat > "$SOURCES" <<EOF
deb [arch=$ARCH signed-by=$KEYRING] https://deb.debian.org/debian forky main
EOF

apt_common() {
    apt-get         -o "Dir::Etc::sourcelist=$SOURCES"         -o "Dir::Etc::sourceparts=-"         -o "Dir::State=$STATE"         -o "Dir::State::status=$STATUS"         -o "Dir::State::lists=$LISTS"         -o "Dir::Cache=$CACHE"         -o "Dir::Cache::archives=$ARCHIVES"         -o "APT::Architecture=$ARCH"         -o "APT::Architectures::=$ARCH"         -o "Acquire::Languages=none"         -o "APT::Install-Recommends=false"         -o "APT::Install-Suggests=false"         "$@"
}

echo "==> Updating isolated forky package metadata"
apt_common update

echo "==> Downloading libdnf5 development/runtime dependency closure"
apt_common --download-only -y --no-install-recommends install     libdnf5-dev libdnf5-2

count=0
for deb in "$ARCHIVES"/*.deb; do
    [ -f "$deb" ] || continue
    dpkg-deb -x "$deb" "$OUT"
    count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
    echo "ERR: No packages were downloaded for the libdnf5 sysroot" >&2
    exit 1
fi

rm -rf "$APTROOT"

if [ ! -f "$OUT/usr/include/libdnf5/base/base.hpp" ]; then
    echo "ERR: libdnf5 headers were not extracted" >&2
    exit 1
fi

if ! sysroot_complete; then
    echo "ERR: libdnf5 sysroot is incomplete after dependency extraction" >&2
    exit 1
fi

echo "==> libdnf5 $ARCH sysroot ready: $OUT"
