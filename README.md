# AdavaLinux

AdavaLinux is a small educational Linux distribution built around a custom Linux kernel, BusyBox userspace, and Adava's `syspckg`. The project is mainly intended for learning how a Linux system boots, builds, and runs.

UEFI graphics is supported, and QEMU is the recommended environment for testing.

## Requirements on x86_64 Hosts

Install the tools required for building the kernel, BusyBox, initramfs images, and GRUB-based ISO outputs:

```sh
sudo apt update
sudo apt install -y \
  build-essential bc bison flex libssl-dev libelf-dev pahole \
  cpio gzip xz-utils tar wget rsync \
  grub-pc-bin grub-efi-amd64-bin xorriso mtools \
  qemu-system-x86 ovmf
```

## Requirements on arm64 Hosts

Cross-building for the fixed `x86_64` target is supported. Install the normal build tools plus the cross toolchain:

```sh
sudo apt update
sudo apt install -y \
  build-essential bc bison flex libssl-dev libelf-dev dwarves \
  cpio gzip xz-utils tar wget ca-certificates file \
  grub-pc-bin grub-efi-amd64-bin xorriso mtools \
  qemu-system-x86 ovmf \
  gcc-x86-64-linux-gnu binutils-x86-64-linux-gnu
```

Enable the `amd64` architecture once:

```sh
sudo dpkg --add-architecture amd64
sudo apt update
```

## Download the Sources

Clone this repository and download the kernel and BusyBox source trees expected by `build.sh`:

```sh
git clone https://github.com/adamcir/AdavaLinux
cd AdavaLinux

wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.19.11.tar.xz
tar -xf linux-6.19.11.tar.xz

wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar -xf busybox-1.36.1.tar.bz2
```

## Build

Build everything with Make:

```sh
make all
```

On `arm64`, use the cross toolchain explicitly:

```sh
CROSS_COMPILE=x86_64-linux-gnu- make all
```

The build can also be run step by step:

```sh
make tools
make kernel
make busybox
make iso
```

`make busybox` only compiles BusyBox. `make iso` installs BusyBox into the generated root filesystems, copies templates, packs both initramfs images, and creates the GRUB ISO outputs. Run `make kernel` and `make busybox` first when those artifacts are missing or stale.

## Output Files

After a successful build, the main artifacts are written to `out/`:

- `out/adavalinux-bios.iso`
- `out/adavalinux-uefi.iso`
- `out/vmlinuz-6.19.11`
- `out/initramfs-installer.gz`
- `out/initramfs-disk.gz`

## Run in QEMU

### BIOS ISO

```sh
make run-bios
```

### UEFI ISO

Boot the UEFI ISO:

```sh
make run-uefi
```

The Makefile creates separate writable OVMF variable files in `out/` for ISO, install, and HDD boot targets.

If KVM is not available, remove `,accel=kvm` and `-cpu host`.

## Install to a QEMU Disk

Boot the installer ISO with the target disk attached. The disk image is created automatically if it does not exist:

```sh
make run-bios-install
make run-uefi-install
```

Inside AdavaLinux, run the installer:

```sh
# INSTALL_MEDIA is the device you booted the installer from.
# DISK is the destination disk.
INSTALL_MEDIA=/dev/sdb DISK=/dev/sda ./install.sh
```

After installation, boot from the disk image:

```sh
make run-bios-hdd
make run-uefi-hdd
```

The BIOS and UEFI HDD boot targets use separate qcow2 images in `out/`:

- `out/adavalinux-bios-hdd.qcow2`
- `out/adavalinux-uefi-hdd.qcow2`
---
*AdavaLinux - by adamcir (Adava) AdavaSoftware in 2026. The OS is under license GPLv2.0*
