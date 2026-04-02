#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$PROJECT_DIR/out"
ROOTFS_DIR="$PROJECT_DIR/rootfs"
DISK_INITRAMFS_DIR="$PROJECT_DIR/disk-initramfs"
ISO_DIR="$PROJECT_DIR/iso"
ISO_OUT_BIOS="$OUT_DIR/adavalinux-bios.iso"
ISO_OUT_UEFI="$OUT_DIR/adavalinux-uefi.iso"
TOOLCACHE_DIR="$PROJECT_DIR/.toolcache"

KERNEL_DIR="$PROJECT_DIR/linux-6.18.8"
BUSYBOX_DIR="$PROJECT_DIR/busybox-1.36.1"
FILESFORLINUX_ROOTFS_DIR="$PROJECT_DIR/filesforlinux/rootfs"
FILESFORLINUX_DISK_INITRAMFS_DIR="$PROJECT_DIR/filesforlinux/initramfs-disk"
FILESFORLINUX_ISO_DIR="$PROJECT_DIR/filesforlinux/iso"
OUT_INSTALLER_INITRAMFS_NAME="initramfs-installer.gz"
OUT_DISK_INITRAMFS_NAME="initramfs-disk.gz"

JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
TARGET_ARCH="${TARGET_ARCH:-x86_64}"
CROSS_COMPILE="${CROSS_COMPILE:-}"
KERNEL_DEFCONFIG="${KERNEL_DEFCONFIG:-defconfig}"
HOST_UNAME="${HOST_UNAME:-$(uname -m 2>/dev/null || echo unknown)}"
SYSPCKG_SYSROOT="${SYSPCKG_SYSROOT:-}"
GRUB_I386_PC_DIR="${GRUB_I386_PC_DIR:-}"
GRUB_X86_64_EFI_DIR="${GRUB_X86_64_EFI_DIR:-}"

say() { printf "\n==> %s\n" "$*"; }
die() { printf "\nERROR: %s\n" "$*" >&2; exit 1; }

need_cmd() { command -v "$1" >/dev/null 2>&1 || die "Missing command: $1"; }

kmake() {
  if [ -n "$CROSS_COMPILE" ]; then
    make ARCH="$KERNEL_ARCH" CROSS_COMPILE="$CROSS_COMPILE" "$@"
  else
    make ARCH="$KERNEL_ARCH" "$@"
  fi
}

bbmake() {
  if [ -n "$CROSS_COMPILE" ]; then
    make ARCH="$BUSYBOX_ARCH" CROSS_COMPILE="$CROSS_COMPILE" "$@"
  else
    make ARCH="$BUSYBOX_ARCH" "$@"
  fi
}

copy_one_lib() {
  src="$1"
  [ -e "$src" ] || return 0

  dest="$ROOTFS_DIR$src"
  mkdir -p "$(dirname "$dest")"

  cp -aL "$src" "$dest" 2>/dev/null || cp -L "$src" "$dest"

  if [ -L "$src" ]; then
    tgt="$(readlink -f "$src" 2>/dev/null || true)"
    if [ -n "$tgt" ] && [ -f "$tgt" ]; then
      dest2="$ROOTFS_DIR$tgt"
      mkdir -p "$(dirname "$dest2")"
      cp -a "$tgt" "$dest2" 2>/dev/null || cp "$tgt" "$dest2" || true
    fi
  fi
}

copy_deps_for_binary() {
  bin="$1"
  [ -x "$bin" ] || return 0

  ldd "$bin" 2>/dev/null | while IFS= read -r line; do
    case "$line" in
      *" => not found"*)
        ;;
      /*)
        set -- $line
        copy_one_lib "$1"
        ;;
      *"=> "/*)
        set -- $line
        copy_one_lib "$3"
        ;;
    esac
  done
}

copy_lib_from_sysroot() {
  rel="$1"
  for base in /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu; do
    src="$SYSPCKG_SYSROOT$base/$rel"
    if [ -f "$src" ]; then
      dest="$ROOTFS_DIR/lib/x86_64-linux-gnu/$rel"
      mkdir -p "$(dirname "$dest")"
      cp -a "$src" "$dest"
      return 0
    fi
  done
  return 1
}

prepare_syspckg_runtime_from_sysroot() {
  [ -n "$SYSPCKG_SYSROOT" ] || return 1

  if [ ! -d "$SYSPCKG_SYSROOT" ]; then
    die "SYSPCKG_SYSROOT neexistuje: $SYSPCKG_SYSROOT"
  fi

  mkdir -p "$ROOTFS_DIR/lib/x86_64-linux-gnu" "$ROOTFS_DIR/lib64"
  copy_lib_from_sysroot "ld-linux-x86-64.so.2" || \
    die "SYSPCKG_SYSROOT is missing ld-linux-x86-64.so.2 in lib or usr/lib for x86_64"
  ln -sf ../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 "$ROOTFS_DIR/lib64/ld-linux-x86-64.so.2"

  # Minimal runtime set for dynamically linked syspckg binary.
  for lib in libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libgcc_s.so.1 libstdc++.so.6; do
    copy_lib_from_sysroot "$lib" || true
  done
}

ensure_amd64_arch_enabled() {
  if dpkg --print-foreign-architectures 2>/dev/null | grep -qx amd64; then
    return 0
  fi

  if [ "$(id -u)" -eq 0 ]; then
    say "Enabling dpkg amd64 architecture (one-time setup)"
    dpkg --add-architecture amd64
    apt update
    return 0
  fi

  die "amd64 architecture is not enabled in dpkg. Run once: sudo dpkg --add-architecture amd64 && sudo apt update"
}

ensure_amd64_sysroot() {
  if [ -n "$SYSPCKG_SYSROOT" ]; then
    [ -f "$SYSPCKG_SYSROOT/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ] || \
    [ -f "$SYSPCKG_SYSROOT/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ] || \
      die "SYSPCKG_SYSROOT does not contain the loader in lib or usr/lib x86_64-linux-gnu"
    return 0
  fi

  for cand in \
    /tmp/amd64-libc \
    /usr/x86_64-linux-gnu \
    /usr/local/x86_64-linux-gnu \
    "$TOOLCACHE_DIR/amd64-sysroot"
  do
    if [ -f "$cand/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ] || \
       [ -f "$cand/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ]; then
      SYSPCKG_SYSROOT="$cand"
      return 0
    fi
  done

  need_cmd apt
  need_cmd dpkg-deb
  ensure_amd64_arch_enabled

  say "Bootstrapping amd64 sysroot for syspckg"
  mkdir -p "$TOOLCACHE_DIR/amd64-sysroot" "$TOOLCACHE_DIR/downloads"
  rm -rf "$TOOLCACHE_DIR/amd64-sysroot"/*
  (
    cd "$TOOLCACHE_DIR/downloads"
    rm -f libc6_*_amd64.deb libgcc-s1_*_amd64.deb libstdc++6_*_amd64.deb
    apt download libc6:amd64 libgcc-s1:amd64 libstdc++6:amd64
    dpkg-deb -x libc6_*_amd64.deb "$TOOLCACHE_DIR/amd64-sysroot"
    dpkg-deb -x libgcc-s1_*_amd64.deb "$TOOLCACHE_DIR/amd64-sysroot"
    dpkg-deb -x libstdc++6_*_amd64.deb "$TOOLCACHE_DIR/amd64-sysroot"
  )

  SYSPCKG_SYSROOT="$TOOLCACHE_DIR/amd64-sysroot"
  [ -f "$SYSPCKG_SYSROOT/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ] || \
  [ -f "$SYSPCKG_SYSROOT/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2" ] || \
    die "Failed to prepare amd64 sysroot with ld-linux-x86-64.so.2"
}

ensure_grub_i386_pc_modules() {
  if [ -n "$GRUB_I386_PC_DIR" ] && [ -d "$GRUB_I386_PC_DIR" ]; then
    return 0
  fi

  if [ -d /usr/lib/grub/i386-pc ]; then
    GRUB_I386_PC_DIR="/usr/lib/grub/i386-pc"
    return 0
  fi

  need_cmd apt
  need_cmd dpkg-deb
  ensure_amd64_arch_enabled

  say "Bootstrapping GRUB i386-pc modules"
  mkdir -p "$TOOLCACHE_DIR/grub-amd64" "$TOOLCACHE_DIR/downloads"
  rm -rf "$TOOLCACHE_DIR/grub-amd64"/*
  (
    cd "$TOOLCACHE_DIR/downloads"
    rm -f grub-pc-bin_*_amd64.deb
    apt download grub-pc-bin:amd64
    dpkg-deb -x grub-pc-bin_*_amd64.deb "$TOOLCACHE_DIR/grub-amd64"
  )

  GRUB_I386_PC_DIR="$TOOLCACHE_DIR/grub-amd64/usr/lib/grub/i386-pc"
  [ -d "$GRUB_I386_PC_DIR" ] || die "Failed to prepare GRUB i386-pc modules"
}

ensure_grub_x86_64_efi_modules() {
  if [ -n "$GRUB_X86_64_EFI_DIR" ] && [ -d "$GRUB_X86_64_EFI_DIR" ]; then
    return 0
  fi

  if [ -d /usr/lib/grub/x86_64-efi ]; then
    GRUB_X86_64_EFI_DIR="/usr/lib/grub/x86_64-efi"
    return 0
  fi

  need_cmd apt
  need_cmd dpkg-deb
  ensure_amd64_arch_enabled

  say "Bootstrapping GRUB x86_64-efi modules"
  mkdir -p "$TOOLCACHE_DIR/grub-efi-amd64" "$TOOLCACHE_DIR/downloads"
  rm -rf "$TOOLCACHE_DIR/grub-efi-amd64"/*
  (
    cd "$TOOLCACHE_DIR/downloads"
    rm -f grub-efi-amd64-bin_*_amd64.deb
    apt download grub-efi-amd64-bin:amd64
    dpkg-deb -x grub-efi-amd64-bin_*_amd64.deb "$TOOLCACHE_DIR/grub-efi-amd64"
  )

  GRUB_X86_64_EFI_DIR="$TOOLCACHE_DIR/grub-efi-amd64/usr/lib/grub/x86_64-efi"
  [ -d "$GRUB_X86_64_EFI_DIR" ] || die "Failed to prepare GRUB x86_64-efi modules"
}

[ -d "$KERNEL_DIR" ] || die "Directory not found: $KERNEL_DIR"
[ -d "$BUSYBOX_DIR" ] || die "Directory not found: $BUSYBOX_DIR"

need_cmd make
need_cmd gcc
need_cmd cpio
need_cmd gzip
need_cmd ldd
need_cmd tar
need_cmd file
need_cmd dpkg

case "$HOST_UNAME" in
  x86_64|amd64)
    HOST_ARCH="x86_64"
    ;;
  aarch64|arm64)
    HOST_ARCH="arm64"
    ;;
  *)
    HOST_ARCH="$HOST_UNAME"
    ;;
esac

if [ "$TARGET_ARCH" != "x86_64" ]; then
  die "AdavaLinux target is fixed to x86_64. Use TARGET_ARCH=x86_64 (or leave it unset)."
fi

KERNEL_ARCH="x86_64"
BUSYBOX_ARCH="x86_64"
KERNEL_IMAGE_REL="arch/x86/boot/bzImage"
KERNEL_DIR_BASENAME="${KERNEL_DIR##*/}"
KERNEL_VERSION="${KERNEL_DIR_BASENAME#linux-}"
OUT_KERNEL_NAME="vmlinuz-$KERNEL_VERSION"
SYSPCKG_MATCH="x86-64"

if [ "$HOST_ARCH" != "x86_64" ] && [ -z "$CROSS_COMPILE" ]; then
  CROSS_COMPILE="x86_64-linux-gnu-"
fi

if [ -n "$CROSS_COMPILE" ]; then
  need_cmd "${CROSS_COMPILE}gcc"
  TOOLCHAIN_MACHINE="$("${CROSS_COMPILE}gcc" -dumpmachine 2>/dev/null || true)"
  case "$TOOLCHAIN_MACHINE" in
    *x86_64*)
      ;;
    *)
      die "CROSS_COMPILE toolchain is not x86_64 (gcc -dumpmachine => $TOOLCHAIN_MACHINE)"
      ;;
  esac
fi

mkdir -p "$OUT_DIR"
mkdir -p "$TOOLCACHE_DIR"

say "Project:  $PROJECT_DIR"
say "Kernel:   $KERNEL_DIR"
say "BusyBox:  $BUSYBOX_DIR"
say "Jobs:     $JOBS"
say "Host:     $HOST_ARCH ($HOST_UNAME)"
say "Target:   $TARGET_ARCH"
say "Cross:    ${CROSS_COMPILE:-<native>}"
say "Sysroot:  ${SYSPCKG_SYSROOT:-<auto>}"

say "Cleaning generated outputs (out/, iso/, rootfs/, disk-initramfs/)"
rm -rf "$OUT_DIR" "$ISO_DIR" "$ROOTFS_DIR" "$DISK_INITRAMFS_DIR"
mkdir -p "$OUT_DIR"

say "Building kernel ($OUT_KERNEL_NAME)"
cd "$KERNEL_DIR"

say "Preparing kernel config: $KERNEL_DEFCONFIG"
kmake "$KERNEL_DEFCONFIG"
KERNEL_CFG_FRAGMENT="$PROJECT_DIR/filesforlinux/kernel.config"
if [ -f "$KERNEL_CFG_FRAGMENT" ]; then
  say "Applying kernel config fragment: $KERNEL_CFG_FRAGMENT"
  CFG_TOOL="$KERNEL_DIR/scripts/config"
  [ -x "$CFG_TOOL" ] || die "Missing kernel config tool: $CFG_TOOL"
  while IFS= read -r line; do
    case "$line" in
      ""|\#*) continue ;;
      CONFIG_*=y)
        "$CFG_TOOL" --enable "${line%%=*}"
        ;;
      CONFIG_*=m)
        "$CFG_TOOL" --module "${line%%=*}"
        ;;
      CONFIG_*=n)
        "$CFG_TOOL" --disable "${line%%=*}"
        ;;
      *)
        die "Invalid kernel config line: $line"
        ;;
    esac
  done < "$KERNEL_CFG_FRAGMENT"
  kmake olddefconfig
fi

kmake -j"$JOBS"
KERNEL_IMAGE="$KERNEL_DIR/$KERNEL_IMAGE_REL"
[ -f "$KERNEL_IMAGE" ] || die "Kernel image not found: $KERNEL_IMAGE"

say "Preparing rootfs layout"
mkdir -p "$ROOTFS_DIR/bin" "$ROOTFS_DIR/sbin" "$ROOTFS_DIR/etc" \
         "$ROOTFS_DIR/proc" "$ROOTFS_DIR/sys" "$ROOTFS_DIR/dev" \
         "$ROOTFS_DIR/usr/bin" "$ROOTFS_DIR/usr/sbin" "$ROOTFS_DIR/tmp" \
         "$ROOTFS_DIR/usr/share" "$ROOTFS_DIR/lib" "$ROOTFS_DIR/lib64" \
         "$ROOTFS_DIR/root"
chmod 1777 "$ROOTFS_DIR/tmp"

say "Preparing disk initramfs layout"
mkdir -p "$DISK_INITRAMFS_DIR/bin" "$DISK_INITRAMFS_DIR/sbin" \
         "$DISK_INITRAMFS_DIR/proc" "$DISK_INITRAMFS_DIR/sys" \
         "$DISK_INITRAMFS_DIR/dev" "$DISK_INITRAMFS_DIR/newroot" \
         "$DISK_INITRAMFS_DIR/tmp"
chmod 1777 "$DISK_INITRAMFS_DIR/tmp"

say "Building BusyBox (static) and installing to rootfs"
cd "$BUSYBOX_DIR"
bbmake distclean
bbmake defconfig

if grep -q '^# CONFIG_STATIC is not set' .config; then
  sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
elif grep -q '^CONFIG_STATIC=' .config; then
  sed -i 's/^CONFIG_STATIC=.*/CONFIG_STATIC=y/' .config
else
  printf "\nCONFIG_STATIC=y\n" >> .config
fi

if grep -q '^CONFIG_TC=y' .config; then
  sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/' .config
fi

if grep -q '^# CONFIG_SETSID is not set' .config; then
  sed -i 's/^# CONFIG_SETSID is not set/CONFIG_SETSID=y/' .config
fi

bbmake oldconfig
bbmake -j"$JOBS"
bbmake CONFIG_PREFIX="$ROOTFS_DIR" install
bbmake CONFIG_PREFIX="$DISK_INITRAMFS_DIR" install

say "Copying rootfs templates from filesforlinux"
[ -d "$FILESFORLINUX_ROOTFS_DIR" ] || die "Rootfs template directory not found: $FILESFORLINUX_ROOTFS_DIR"
cp -a "$FILESFORLINUX_ROOTFS_DIR/." "$ROOTFS_DIR/"
[ -d "$FILESFORLINUX_DISK_INITRAMFS_DIR" ] || die "disk initramfs template directory not found: $FILESFORLINUX_DISK_INITRAMFS_DIR"
cp -a "$FILESFORLINUX_DISK_INITRAMFS_DIR/." "$DISK_INITRAMFS_DIR/"

for req in etc/os-release init etc/motd etc/profile etc/inittab etc/init.d/rcS usr/share/udhcpc/default.script; do
  [ -f "$ROOTFS_DIR/$req" ] || die "Required file missing in rootfs templates: $req"
done
chmod +x "$ROOTFS_DIR/init" "$ROOTFS_DIR/etc/init.d/rcS" "$ROOTFS_DIR/usr/share/udhcpc/default.script"
[ -f "$DISK_INITRAMFS_DIR/init" ] || die "Required file missing in disk initramfs templates: init"
chmod +x "$DISK_INITRAMFS_DIR/init"

say "Installing Syspckg and bundled packages into rootfs"

SYSPCKG_BIN="$FILESFORLINUX_ROOTFS_DIR/usr/bin/syspckg"
[ -x "$SYSPCKG_BIN" ] || die "Executable syspckg not found: $SYSPCKG_BIN"
SYSPCKG_INFO="$(file -b "$SYSPCKG_BIN" 2>/dev/null || true)"
case "$SYSPCKG_INFO" in
  *"$SYSPCKG_MATCH"*)
    ;;
  *)
    die "syspckg is not an x86_64 binary: $SYSPCKG_INFO"
    ;;
esac

mkdir -p "$ROOTFS_DIR/usr/bin" "$ROOTFS_DIR/usr/share/syspckg/packages"
cp -a "$SYSPCKG_BIN" "$ROOTFS_DIR/usr/bin/syspckg"
ln -sf /usr/bin/syspckg "$ROOTFS_DIR/bin/syspckg"

say "Copying runtime loader + required shared libraries for syspckg into rootfs"
if [ -f /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 ]; then
  copy_one_lib "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
  ln -sf ../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 "$ROOTFS_DIR/lib64/ld-linux-x86-64.so.2"
else
  ensure_amd64_sysroot
  prepare_syspckg_runtime_from_sysroot
fi

say "Copying minimal terminfo into rootfs"
if [ -d /usr/share/terminfo ]; then
  mkdir -p "$ROOTFS_DIR/usr/share/terminfo"
  if [ -d /usr/share/terminfo/l ]; then
    mkdir -p "$ROOTFS_DIR/usr/share/terminfo/l"
    cp -a /usr/share/terminfo/l/linux "$ROOTFS_DIR/usr/share/terminfo/l/" 2>/dev/null || true
  fi
  if [ -f /usr/share/terminfo/x/xterm-256color ]; then
    mkdir -p "$ROOTFS_DIR/usr/share/terminfo/x"
    cp -a /usr/share/terminfo/x/xterm-256color "$ROOTFS_DIR/usr/share/terminfo/x/" 2>/dev/null || true
  fi
fi

if [ "$HOST_ARCH" = "x86_64" ]; then
  copy_deps_for_binary "$ROOTFS_DIR/usr/bin/syspckg"
else
  ensure_amd64_sysroot
  prepare_syspckg_runtime_from_sysroot
fi

if [ -f "$FILESFORLINUX_ROOTFS_DIR/install.sh" ]; then
  say "Copying install.sh into initramfs root"
  cp -f "$FILESFORLINUX_ROOTFS_DIR/install.sh" "$ROOTFS_DIR/install.sh"
  chmod +x "$ROOTFS_DIR/install.sh"
else
  say "install.sh not found in filesforlinux/rootfs -> skipping initramfs copy"
fi

if [ -f "$FILESFORLINUX_ROOTFS_DIR/etc/syspckg-source" ]; then
  say "Copying syspckg source config into rootfs"
  mkdir -p "$ROOTFS_DIR/etc" "$ROOTFS_DIR/usr/share/syspckg"
  cp -f "$FILESFORLINUX_ROOTFS_DIR/etc/syspckg-source" "$ROOTFS_DIR/etc/syspckg-source"
  if [ -f "$FILESFORLINUX_ROOTFS_DIR/usr/share/syspckg/source-url" ]; then
    cp -f "$FILESFORLINUX_ROOTFS_DIR/usr/share/syspckg/source-url" "$ROOTFS_DIR/usr/share/syspckg/source-url"
  else
    cp -f "$FILESFORLINUX_ROOTFS_DIR/etc/syspckg-source" "$ROOTFS_DIR/usr/share/syspckg/source-url"
  fi
else
  say "syspckg-source not found in filesforlinux/rootfs/etc -> using syspckg built-in default URL"
fi

say "Packing $OUT_INSTALLER_INITRAMFS_NAME"
cd "$ROOTFS_DIR"
find . -print0 | cpio --null -ov --format=newc | gzip -9 > "$OUT_DIR/$OUT_INSTALLER_INITRAMFS_NAME"

say "Packing $OUT_DISK_INITRAMFS_NAME"
cd "$DISK_INITRAMFS_DIR"
find . -print0 | cpio --null -ov --format=newc | gzip -9 > "$OUT_DIR/$OUT_DISK_INITRAMFS_NAME"

say "Copying kernel image"
cp -f "$KERNEL_IMAGE" "$OUT_DIR/$OUT_KERNEL_NAME"

say "Building ISO (GRUB)"
mkdir -p "$ISO_DIR/boot/grub"
cp -f "$OUT_DIR/$OUT_KERNEL_NAME" "$ISO_DIR/boot/$OUT_KERNEL_NAME"
cp -f "$OUT_DIR/$OUT_INSTALLER_INITRAMFS_NAME" "$ISO_DIR/boot/$OUT_INSTALLER_INITRAMFS_NAME"
cp -f "$OUT_DIR/$OUT_DISK_INITRAMFS_NAME" "$ISO_DIR/boot/$OUT_DISK_INITRAMFS_NAME"
if [ -f "$FILESFORLINUX_ISO_DIR/install.sh" ]; then
  cp -f "$FILESFORLINUX_ISO_DIR/install.sh" "$ISO_DIR/install.sh"
else
  say "install.sh not found in filesforlinux/iso -> skipping"
fi

GRUB_CFG_TEMPLATE="$FILESFORLINUX_ISO_DIR/boot/grub/grub.cfg"
[ -f "$GRUB_CFG_TEMPLATE" ] || die "Missing GRUB configuration: $GRUB_CFG_TEMPLATE"
cp -f "$GRUB_CFG_TEMPLATE" "$ISO_DIR/boot/grub/grub.cfg"
sed -i "s|__KERNEL_IMAGE__|$OUT_KERNEL_NAME|g" "$ISO_DIR/boot/grub/grub.cfg"

if command -v grub-mkrescue >/dev/null 2>&1; then
  if ! command -v grub-mkimage >/dev/null 2>&1; then
    die "grub-mkimage is missing. Install GRUB packages with BIOS (i386-pc) modules."
  fi
  ensure_grub_i386_pc_modules
  ensure_grub_x86_64_efi_modules
  need_cmd mformat
  need_cmd mcopy
  if ! grub-mkimage -d "$GRUB_I386_PC_DIR" -O i386-pc -p /boot/grub -o "$OUT_DIR/grub-core-bios.img" biosdisk iso9660 >/dev/null 2>&1; then
    die "Missing GRUB BIOS i386-pc modules. The ISO will not boot in SeaBIOS. Install grub i386-pc modules."
  fi
  if ! grub-mkimage -d "$GRUB_X86_64_EFI_DIR" -O x86_64-efi -p /boot/grub -o "$OUT_DIR/grub-core-uefi.efi" iso9660 normal configfile >/dev/null 2>&1; then
    die "Missing GRUB UEFI x86_64-efi modules. Install grub-efi-amd64-bin or provide GRUB x86_64-efi modules."
  fi
  rm -f "$OUT_DIR/grub-core-bios.img"
  rm -f "$OUT_DIR/grub-core-uefi.efi"
  grub-mkrescue --directory="$GRUB_I386_PC_DIR" -o "$ISO_OUT_BIOS" "$ISO_DIR" >/dev/null
  say "ISO created: $ISO_OUT_BIOS"
  grub-mkrescue --directory="$GRUB_X86_64_EFI_DIR" -o "$ISO_OUT_UEFI" "$ISO_DIR" >/dev/null
  say "ISO created: $ISO_OUT_UEFI"
else
  die "grub-mkrescue is missing. Cannot create ISO."
fi

say "Sanity check: installer initramfs contains init + bin/sh"
gzip -dc "$OUT_DIR/$OUT_INSTALLER_INITRAMFS_NAME" | cpio -it | grep -E '^init$|^bin/sh$' >/dev/null \
  || die "Installer initramfs does not contain init or bin/sh"

say "Sanity check: disk initramfs contains init + bin/sh"
gzip -dc "$OUT_DIR/$OUT_DISK_INITRAMFS_NAME" | cpio -it | grep -E '^init$|^bin/sh$' >/dev/null \
  || die "Disk initramfs does not contain init or bin/sh"

cat <<EOF

Done

Run in QEMU:
EOF

cat <<EOF
  qemu-system-x86_64 -cdrom "$ISO_OUT_BIOS" -m 1024M
  qemu-system-x86_64 -cdrom "$ISO_OUT_UEFI" -m 1024M
EOF
