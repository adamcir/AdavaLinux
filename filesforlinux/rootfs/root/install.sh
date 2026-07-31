#!/bin/sh
#WARNING!! This script erases the selected disk
set -eu

DISK="${DISK:-}"
INSTALL_MEDIA="${INSTALL_MEDIA:-auto}"
K_VERSION="${K_VERSION:-$(uname -r 2>/dev/null || true)}"
FS_TYPE="ext2"

die() { echo "ERROR: $*" >&2; exit 1; }

hash_password() {
  pass="$1"
  if command -v mkpasswd >/dev/null 2>&1; then
    mkpasswd -m sha-512 "$pass"
    return $?
  fi
  if command -v openssl >/dev/null 2>&1; then
    openssl passwd -6 "$pass"
    return $?
  fi
  return 1
}

prompt_password() {
  label="$1"
  while :; do
    if [ -c /dev/tty ] && command -v stty >/dev/null 2>&1; then
      printf "Enter password for %s: " "$label" >&2
      stty -echo < /dev/tty
      read -r P1 < /dev/tty || true
      stty echo < /dev/tty
      printf "\n" >&2
      printf "Confirm password for %s: " "$label" >&2
      stty -echo < /dev/tty
      read -r P2 < /dev/tty || true
      stty echo < /dev/tty
      printf "\n" >&2
    else
      printf "Enter password for %s: " "$label" >&2
      read -r P1 || true
      printf "Confirm password for %s: " "$label" >&2
      read -r P2 || true
    fi
    [ -n "${P1:-}" ] || { echo "Password cannot be empty."; continue; }
    [ "$P1" = "$P2" ] || { echo "Passwords do not match."; continue; }
    printf "%s\n" "$P1"
    return 0
  done
}

prompt_user_account() {
  if [ -t 0 ]; then
    while :; do
      printf "Enter username for installed system (not root): "
      read -r NEW_USER || true
      case "$NEW_USER" in
        ""|root|*[!a-z0-9_-]*)
          echo "Invalid username. Use lowercase letters, numbers, '_' or '-' and not 'root'."
          ;;
        *)
          break
          ;;
      esac
    done
    PASS1="$(prompt_password "$NEW_USER")"
    ROOT_PASS="$(prompt_password "root")"
  else
    NEW_USER="${USERNAME:-}"
    PASS1="${USERPASS:-}"
    ROOT_PASS="${ROOTPASS:-}"
    [ -n "$NEW_USER" ] || die "Set USERNAME for non-interactive install"
    [ -n "$PASS1" ] || die "Set USERPASS for non-interactive install"
    [ -n "$ROOT_PASS" ] || die "Set ROOTPASS for non-interactive install"
  fi

  USER_HASH="$(hash_password "$PASS1" || true)"
  [ -n "$USER_HASH" ] || die "Cannot hash user password (need mkpasswd or openssl)"
  ROOT_HASH="$(hash_password "$ROOT_PASS" || true)"
  [ -n "$ROOT_HASH" ] || die "Cannot hash root password (need mkpasswd or openssl)"
}

write_user_files() {
  root_dir="$1"
  user_name="$2"
  user_hash="$3"
  root_hash="$4"
  host_name="$5"

  mkdir -p "$root_dir/root" "$root_dir/home/$user_name"
  cat > "$root_dir/etc/passwd" <<EOF
root:x:0:0:root:/root:/bin/sh
$user_name:x:1000:1000:$user_name:/home/$user_name:/bin/sh
EOF
  cat > "$root_dir/etc/shadow" <<EOF
root:$root_hash:0:0:99999:7:::
$user_name:$user_hash:0:0:99999:7:::
EOF
  cat > "$root_dir/etc/group" <<EOF
root:x:0:
$user_name:x:1000:
EOF
  if [ -n "$host_name" ]; then
    printf "%s\n" "$host_name" > "$root_dir/etc/hostname"
    cat > "$root_dir/etc/hosts" <<EOF
127.0.0.1 localhost
127.0.1.1 $host_name
EOF
  fi
  if command -v chown >/dev/null 2>&1; then
    chown root:root "$root_dir/etc/passwd" "$root_dir/etc/shadow" "$root_dir/etc/group" 2>/dev/null || true
    chown 1000:1000 "$root_dir/home/$user_name" 2>/dev/null || true
  fi
  if command -v chmod >/dev/null 2>&1; then
    chmod 644 "$root_dir/etc/passwd" "$root_dir/etc/group" 2>/dev/null || true
    chmod 600 "$root_dir/etc/shadow" 2>/dev/null || true
  fi
}

safe_umount() {
  mnt="$1"
  if awk -v m="$mnt" '$2==m{found=1} END{exit !found}' /proc/mounts 2>/dev/null; then
    umount "$mnt" 2>/dev/null || true
  fi
}

mount_dev() {
  mnt="$1"
  awk -v m="$mnt" '$2==m{print $1; exit}' /proc/mounts 2>/dev/null || true
}

resolve_uuid() {
  dev="$1"
  u=""
  if command -v blkid >/dev/null 2>&1; then
    u="$(blkid -s UUID -o value "$dev" 2>/dev/null || true)"
    if [ -z "$u" ] || echo "$u" | grep -Eq '/dev/|[[:space:]]'; then
      u="$(blkid "$dev" 2>/dev/null | sed -n 's/.*UUID=\"\\([^\"]*\\)\".*/\\1/p' || true)"
    fi
  fi
  if [ -z "$u" ] && command -v busybox >/dev/null 2>&1; then
    u="$(busybox blkid -s UUID -o value "$dev" 2>/dev/null || true)"
    if [ -z "$u" ] || echo "$u" | grep -Eq '/dev/|[[:space:]]'; then
      u="$(busybox blkid "$dev" 2>/dev/null | sed -n 's/.*UUID=\"\\([^\"]*\\)\".*/\\1/p' || true)"
    fi
  fi
  [ -n "$u" ] || return 1
  printf "%s\n" "$u"
}

format_partition() {
  tgt="$1"
  if mkfs.ext2 -F -E nodiscard "$tgt"; then
    :
  else
    echo "mkfs.ext2 with nodiscard failed, retrying basic mkfs.ext2 -F"
    mkfs.ext2 -F "$tgt"
  fi
  if command -v fsck.ext2 >/dev/null 2>&1; then
    fsck.ext2 -f -y "$tgt" || true
  fi
}

parent_disk() {
  case "$1" in
    /dev/sr*) return 1 ;;
    /dev/nvme*n*p[0-9]*) printf "%s\n" "${1%p[0-9]*}" ;;
    /dev/mmcblk*p[0-9]*) printf "%s\n" "${1%p[0-9]*}" ;;
    /dev/*[0-9]) printf "%s\n" "${1%[0-9]*}" ;;
    *) printf "%s\n" "$1" ;;
  esac
}

part1_of_disk() {
  case "$1" in
    /dev/nvme*n[0-9]|/dev/mmcblk[0-9]) printf "%sp1\n" "$1" ;;
    *) printf "%s1\n" "$1" ;;
  esac
}

part_of_disk() {
  disk="$1"
  num="$2"
  case "$disk" in
    /dev/nvme*n[0-9]|/dev/mmcblk[0-9]) printf "%sp%s\n" "$disk" "$num" ;;
    *) printf "%s%s\n" "$disk" "$num" ;;
  esac
}

resolve_grub_platform_dir() {
  platform="$1"
  search_dirs="${GRUB_SEARCH_DIRS:-/usr/lib/grub/$platform:/usr/lib64/grub/$platform:/lib/grub/$platform:/lib64/grub/$platform:/usr/local/lib/grub/$platform:/mnt/install/usr/lib/grub/$platform:/mnt/install/usr/lib64/grub/$platform:/mnt/install/lib/grub/$platform:/mnt/install/lib64/grub/$platform:/mnt/install/rootfs/usr/lib/grub/$platform:/mnt/install/rootfs/usr/lib64/grub/$platform:/mnt/install/packages:/mnt/install/syspckg}"
  old_ifs="${IFS- }"
  IFS=':'
  set -- $search_dirs
  IFS="$old_ifs"
  for base do
    [ -n "$base" ] || continue
    case "$base" in
      */"$platform") cand="$base" ;;
      *) cand="$base/usr/lib/grub/$platform" ;;
    esac
    [ -f "$cand/modinfo.sh" ] || continue
    printf "%s\n" "$cand"
    return 0
  done
  return 1
}

is_supported_disk() {
  case "$1" in
    /dev/sd[a-z]|/dev/vd[a-z]|/dev/xvd[a-z]|/dev/nvme[0-9]n[0-9]|/dev/mmcblk[0-9]) return 0 ;;
    *) return 1 ;;
  esac
}

show_disks_hint() {
  echo "Available disks:"
  for p in /sys/block/*; do
    n="$(basename "$p")"
    case "$n" in
      loop*|ram*|sr*) continue ;;
    esac
    b="/dev/$n"
    [ -b "$b" ] || continue
    sz="$(cat "$p/size" 2>/dev/null || echo 0)"
    mib=$((sz / 2048))
    echo "  $b (${mib} MiB)"
  done
}

confirm_erase() {
  phrase="ERASE $DISK"
  echo "Target disk: $DISK"
  echo "Installer media: $MEDIA_DEV"
  echo "ALL DATA ON $DISK WILL BE PERMANENTLY DESTROYED."
  if [ -t 0 ]; then
    echo "Type exactly: $phrase"
    printf "> "
    read -r answer || die "Confirmation failed"
    [ "$answer" = "$phrase" ] || die "Confirmation text mismatch"
  else
    [ "${CONFIRM_ERASE:-}" = "$phrase" ] || die "Set CONFIRM_ERASE='$phrase' for non-interactive mode"
  fi
}

echo "[1/10] preparing"
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mdev -s 2>/dev/null || true
safe_umount /mnt/root/boot/efi
safe_umount /mnt/root
safe_umount /mnt/install

[ -n "$DISK" ] || { show_disks_hint; die "Set target disk explicitly, e.g. DISK=/dev/sdb sh install.sh"; }
[ -b "$DISK" ] || die "Target disk is not a block device: $DISK"
is_supported_disk "$DISK" || die "Unsupported DISK path '$DISK' (use whole disk, e.g. /dev/sdb or /dev/nvme0n1)"

BOOT_MODE="${BOOT_MODE:-}"
if [ -z "$BOOT_MODE" ]; then
  if [ -t 0 ]; then
    echo "Select boot mode:"
    echo "  1) BIOS (legacy)"
    echo "  2) UEFI"
    printf "> "
    read -r boot_choice || die "Boot mode selection failed"
    case "$boot_choice" in
      1) BOOT_MODE="bios" ;;
      2) BOOT_MODE="uefi" ;;
      *) die "Invalid boot mode selection" ;;
    esac
  else
    BOOT_MODE="bios"
  fi
fi

ACPI_MODE="${ACPI_MODE:-}"
if [ -z "$ACPI_MODE" ]; then
  if [ -t 0 ]; then
    echo "Select ACPI mode:"
    echo "  1) ACPI (default)"
    echo "  2) ACPI=off (old hardware)"
    printf "> "
    read -r acpi_choice || die "ACPI mode selection failed"
    case "$acpi_choice" in
      1) ACPI_MODE="acpi" ;;
      2) ACPI_MODE="off" ;;
      *) die "Invalid ACPI mode selection" ;;
    esac
  else
    ACPI_MODE="acpi"
  fi
fi

PART_SIZE="${PART_SIZE:-}"
if [ -z "$PART_SIZE" ]; then
  if [ -t 0 ]; then
    echo "Root partition size (fdisk format like +1G, +10G)."
    echo "Leave empty to use the default automatically."
    printf "> "
    read -r PART_SIZE || die "Partition size selection failed"
    [ -n "$PART_SIZE" ] || PART_SIZE=""
  else
    PART_SIZE=""
  fi
fi

case "$BOOT_MODE" in
  bios|uefi) ;;
  *) die "Unsupported BOOT_MODE '$BOOT_MODE' (use bios or uefi)" ;;
esac
case "$ACPI_MODE" in
  acpi|off|acpi=off) ;;
  *) die "Unsupported ACPI_MODE '$ACPI_MODE' (use acpi or acpi=off)" ;;
esac

if [ "$ACPI_MODE" = "acpi=off" ]; then
  ACPI_MODE="off"
fi

PART="$(part_of_disk "$DISK" 1)"
EFI_PART=""
if [ "$BOOT_MODE" = "uefi" ]; then
  EFI_PART="$(part_of_disk "$DISK" 1)"
  PART="$(part_of_disk "$DISK" 2)"
fi

echo "[2/10] mounting installer media"
mkdir -p /mnt/install /mnt/root
safe_umount /mnt/install
MEDIA_DEV=""
MOUNTED_DEV="$(mount_dev /mnt/install)"
if [ -n "$MOUNTED_DEV" ]; then
  if [ -f /mnt/install/boot/initramfs-installer.gz ]; then
    MEDIA_DEV="$MOUNTED_DEV"
  else
    safe_umount /mnt/install
  fi
fi
if [ "$INSTALL_MEDIA" = "auto" ]; then
  if [ -z "$MEDIA_DEV" ]; then
    for cand in /dev/sr0 /dev/sda1 /dev/sda; do
      [ -b "$cand" ] || continue
      if mount -o ro "$cand" /mnt/install 2>/dev/null; then
        if [ -f /mnt/install/boot/initramfs-installer.gz ]; then
          MEDIA_DEV="$cand"
          break
        fi
        umount /mnt/install 2>/dev/null || true
      fi
    done
  fi
else
  [ -b "$INSTALL_MEDIA" ] || die "Installer media is not a block device: $INSTALL_MEDIA"
  if [ -z "$MEDIA_DEV" ]; then
    mount -o ro "$INSTALL_MEDIA" /mnt/install || die "Cannot mount installer media: $INSTALL_MEDIA"
    MEDIA_DEV="$INSTALL_MEDIA"
  fi
fi
[ -n "$MEDIA_DEV" ] || die "Installer media not found (tried /dev/sr0, /dev/sda1, /dev/sda). Set INSTALL_MEDIA=/dev/..."

MEDIA_PARENT="$(parent_disk "$MEDIA_DEV" 2>/dev/null || true)"
[ "$MEDIA_DEV" != "$DISK" ] || die "Target disk ($DISK) is the same as installer media ($MEDIA_DEV)"
if [ -n "$MEDIA_PARENT" ] && [ "$MEDIA_PARENT" = "$DISK" ]; then
  die "Target disk ($DISK) is the same physical disk as installer media ($MEDIA_DEV)"
fi

ROOT_SRC="$(awk '$2=="/"{print $1; exit}' /proc/mounts 2>/dev/null || true)"
if [ -n "$ROOT_SRC" ] && [ "${ROOT_SRC#/dev/}" != "$ROOT_SRC" ]; then
  ROOT_PARENT="$(parent_disk "$ROOT_SRC" 2>/dev/null || true)"
  [ -n "$ROOT_PARENT" ] && [ "$ROOT_PARENT" = "$DISK" ] && die "Refusing to erase current root disk: $DISK"
fi

confirm_erase

install_grub_pkg() {
  pkg="$1"
  local_pkg=""
  for base in /mnt/install/packages /mnt/install/syspckg /mnt/install; do
    for cand in "$base/$pkg-"*.syspckg "$base/$pkg.syspckg"; do
      [ -f "$cand" ] || continue
      local_pkg="$cand"
      break
    done
    [ -n "$local_pkg" ] && break
  done
  if [ -n "$local_pkg" ]; then
    echo "Using local package: $local_pkg"
    syspckg install -local "$local_pkg"
  else
    syspckg install "$pkg"
  fi
}

if [ "$BOOT_MODE" = "uefi" ]; then
  install_grub_pkg grub-efi
else
  install_grub_pkg grub-bios
fi

if [ "$BOOT_MODE" = "bios" ]; then
  echo "[3/10] partitioning disk (MBR, 1 partition, 1MiB aligned)"
  (
    echo o
    echo n
    echo p
    echo 1
    echo 2048
    echo "$PART_SIZE"
    echo a
    echo 1
    echo w
  ) | fdisk "$DISK"
else
  echo "[3/10] partitioning disk (GPT, EFI + root)"
  (
    echo g
    echo n
    echo 1
    echo
    echo +512M
    echo t
    echo 1
    echo n
    echo 2
    echo
    echo
    echo w
  ) | fdisk "$DISK"
fi
sync
blockdev --rereadpt "$DISK" 2>/dev/null || true
partprobe "$DISK" 2>/dev/null || true

for _ in 1 2 3 4 5; do
  [ -b "$PART" ] && { [ -z "$EFI_PART" ] || [ -b "$EFI_PART" ]; } && break
  mdev -s 2>/dev/null || true
  sleep 1
done
[ -b "$PART" ] || die "Partition not found after fdisk: $PART"
[ -z "$EFI_PART" ] || [ -b "$EFI_PART" ] || die "EFI partition not found after fdisk: $EFI_PART"

echo "[4/10] formatting $FS_TYPE (compat mode)"
if [ "$BOOT_MODE" = "uefi" ]; then
  if command -v mkfs.vfat >/dev/null 2>&1; then
    mkfs.vfat -F 32 "$EFI_PART"
  else
    die "mkfs.vfat not found for EFI system partition"
  fi
fi
format_partition "$PART"

echo "[5/10] mounting target partition"
mount -t "$FS_TYPE" "$PART" /mnt/root
if [ "$BOOT_MODE" = "uefi" ]; then
  mkdir -p /mnt/root/boot/efi
  mount -t vfat "$EFI_PART" /mnt/root/boot/efi
fi
ROOT_UUID="$(resolve_uuid "$PART" || true)"
ROOT_DEV="$PART"

echo "[6/10] extracting initramfs to disk"
rm -rf /mnt/root/*
cd /mnt/root
gzip -dc /mnt/install/boot/initramfs-installer.gz | cpio -idmv
rm -f /mnt/root/install.sh 2>/dev/null || true
if [ -x /mnt/root/bin/busybox ]; then
  /mnt/root/bin/busybox --install -s /mnt/root/bin
fi
mkdir -p /mnt/root/boot
cp /mnt/install/boot/initramfs-disk.gz /mnt/root/boot/initramfs-disk.gz

echo "[6a/10] creating user account"
prompt_user_account
HOST_NAME="${HOSTNAME:-}"
if [ -z "$HOST_NAME" ]; then
  if [ -t 0 ]; then
    printf "Enter hostname (default: adavalinux): "
    read -r HOST_NAME || true
  fi
  [ -n "$HOST_NAME" ] || HOST_NAME="adavalinux"
fi
write_user_files /mnt/root "$NEW_USER" "$USER_HASH" "$ROOT_HASH" "$HOST_NAME"

echo "[7/10] copying kernel"
KERNEL_NAME=""
for cand in "vmlinuz-$K_VERSION" "vmlinuz-$K_VERSION.gz"; do
  if [ -f "/mnt/install/boot/$cand" ]; then
    KERNEL_NAME="$cand"
    break
  fi
done
if [ -z "$KERNEL_NAME" ]; then
  KERNEL_CANDIDATE="$(ls -1 /mnt/install/boot/vmlinuz-* 2>/dev/null | head -n 1 || true)"
  [ -n "$KERNEL_CANDIDATE" ] || { echo "Kernel not found on installer media"; exit 1; }
  KERNEL_NAME="$(basename "$KERNEL_CANDIDATE")"
fi
cp "/mnt/install/boot/$KERNEL_NAME" "/mnt/root/boot/$KERNEL_NAME"
ln -sf "$KERNEL_NAME" /mnt/root/boot/vmlinuz

echo "[8/10] removing installer script"
rm -f /mnt/root/root/install.sh 2>/dev/null || true

echo "[9/10] writing bootloader config"
if [ -n "$ROOT_UUID" ]; then
  ROOT_ARG="root=UUID=$ROOT_UUID"
else
  ROOT_ARG="root=$ROOT_DEV"
fi

mkdir -p /mnt/root/boot/grub
if [ "$ACPI_MODE" = "off" ]; then
  cat > /mnt/root/boot/grub/grub.cfg <<EOF
set timeout=10
set default=0
terminal_input console
terminal_output console

menuentry "AdavaLinux v1.0" {
	echo 'Loading Linux ...'
	linux /boot/$KERNEL_NAME $ROOT_ARG rootfstype=$FS_TYPE rootwait rootdelay=5 rw console=ttyS0 console=tty1 nomodeset libata.force=noncq acpi=off noapic nolapic irqpoll pci=nomsi quiet
	echo 'Loading initramfs...'
	initrd /boot/initramfs-disk.gz
}

menuentry "AdavaLinux v1.0" {
	echo 'Loading Linux ...'
	linux /boot/$KERNEL_NAME $ROOT_ARG rootfstype=$FS_TYPE rootwait rootdelay=5 rw console=ttyS0 console=tty1 nomodeset libata.force=noncq acpi=off noapic nolapic irqpoll pci=nomsi
	echo 'Loading initramfs...'
	initrd /boot/initramfs-disk.gz
}
EOF
else
  cat > /mnt/root/boot/grub/grub.cfg <<EOF
set timeout=10
set default=0
terminal_input console
terminal_output console

menuentry "AdavaLinux v1.0" {
	echo 'Loading Linux ...'
	linux /boot/$KERNEL_NAME $ROOT_ARG rootfstype=$FS_TYPE rootwait rootdelay=5 rw console=ttyS0 console=tty1 nomodeset libata.force=noncq quiet
	echo 'Loading initramfs...'
	initrd /boot/initramfs-disk.gz
}

menuentry "AdavaLinux v1.0 (debug)" {
	echo 'Loading Linux ...'
	linux /boot/$KERNEL_NAME $ROOT_ARG rootfstype=$FS_TYPE rootwait rootdelay=5 rw console=ttyS0 console=tty1 nomodeset libata.force=noncq
	echo 'Loading initramfs...'
	initrd /boot/initramfs-disk.gz
}
EOF
fi

echo "[10/10] installing bootloader"
if command -v grub-install >/dev/null 2>&1; then
  if [ "$BOOT_MODE" = "uefi" ]; then
    GRUB_PLATFORM_DIR="$(resolve_grub_platform_dir x86_64-efi || true)"
    GRUB_DIR_ARG=""
    [ -n "$GRUB_PLATFORM_DIR" ] && GRUB_DIR_ARG="--directory=$GRUB_PLATFORM_DIR"
    grub-install ${GRUB_DIR_ARG:+$GRUB_DIR_ARG }--target=x86_64-efi --efi-directory=/mnt/root/boot/efi \
      --bootloader-id=AdavaLinux --removable --boot-directory=/mnt/root/boot
  else
    GRUB_PLATFORM_DIR="$(resolve_grub_platform_dir i386-pc || true)"
    [ -n "$GRUB_PLATFORM_DIR" ] || die "GRUB BIOS modules not found (missing modinfo.sh for i386-pc)"
    grub-install --directory="$GRUB_PLATFORM_DIR" --target=i386-pc --boot-directory=/mnt/root/boot \
      --force --recheck --no-floppy \
      --compress=no --core-compress=none --no-rs-codes --disk-module=biosdisk \
      --modules="biosdisk part_msdos ext2" "$DISK"
    if command -v grub-bios-setup >/dev/null 2>&1; then
      grub-bios-setup -v -d /mnt/root/boot/grub/i386-pc "$DISK" || die "grub-bios-setup failed"
    fi
    [ -f /mnt/root/boot/grub/i386-pc/core.img ] || die "Missing /boot/grub/i386-pc/core.img after grub-install"
    sig="$(dd if="$DISK" bs=512 count=1 2>/dev/null | tail -c 2 | od -An -tx1 | tr -d ' \n')"
    [ "$sig" = "55aa" ] || die "Invalid MBR signature ($sig) after grub-install"
  fi
else
  echo "grub-install not found after grub install"
  exit 1
fi

sync
cd /
safe_umount /mnt/root/boot/efi
safe_umount /mnt/root
safe_umount /mnt/install

echo "Done. Detach ISO and reboot."
