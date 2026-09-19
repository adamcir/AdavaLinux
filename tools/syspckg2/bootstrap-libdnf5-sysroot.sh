#!/bin/sh
set -eu

OUT="${1:?usage: bootstrap-libdnf5-sysroot.sh OUT_DIR [ARCH]}"
ARCH="${2:-amd64}"
DOWNLOADS="$OUT/.downloads"

need() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERR: Missing command: $1" >&2
        exit 1
    }
}

need apt
need apt-cache
need dpkg-deb

if [ -f "$OUT/usr/include/libdnf5/base/base.hpp" ] &&
   { [ -e "$OUT/usr/lib/x86_64-linux-gnu/libdnf5.so" ] ||
     [ -e "$OUT/usr/lib/x86_64-linux-gnu/libdnf5.so.2" ]; }; then
    exit 0
fi

mkdir -p "$OUT" "$DOWNLOADS"
rm -rf "$OUT/usr" "$OUT/lib" "$OUT/lib64" "$DOWNLOADS"/*

roots="libdnf5-dev libdnf5-2"

deps_for() {
    apt-cache depends --recurse \
        --no-recommends --no-suggests --no-conflicts --no-breaks \
        --no-replaces --no-enhances "$@" 2>/dev/null |
    sed -n -E 's/^[ |]*(Pre)?Depends:[[:space:]]+([^ <|]+).*/\2/p' |
    sed 's/:any$//' |
    sort -u
}

specs=""
for pkg in $roots; do
    specs="$specs $pkg:$ARCH"
done

deps="$(deps_for $specs || true)"
packages="$roots $deps"

cd "$DOWNLOADS"
for pkg in $packages; do
    [ -n "$pkg" ] || continue
    case "$pkg" in
        *:*) spec="$pkg" ;;
        *) spec="$pkg:$ARCH" ;;
    esac

    echo "==> libdnf5 sysroot: downloading $spec"
    if ! apt download "$spec" >/dev/null 2>&1; then
        # Architecture-independent packages do not always accept :amd64.
        echo "==> libdnf5 sysroot: retrying $pkg"
        apt download "$pkg" >/dev/null
    fi
done

for deb in ./*.deb; do
    [ -f "$deb" ] || continue
    dpkg-deb -x "$deb" "$OUT"
done

rm -rf "$DOWNLOADS"

if [ ! -f "$OUT/usr/include/libdnf5/base/base.hpp" ]; then
    echo "ERR: libdnf5 headers were not extracted" >&2
    exit 1
fi

if [ ! -e "$OUT/usr/lib/x86_64-linux-gnu/libdnf5.so" ] &&
   [ ! -e "$OUT/usr/lib/x86_64-linux-gnu/libdnf5.so.2" ]; then
    echo "ERR: libdnf5 runtime library was not extracted" >&2
    exit 1
fi

echo "==> libdnf5 amd64 sysroot ready: $OUT"
