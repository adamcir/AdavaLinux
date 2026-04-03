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

Make the build script executable and run it:

```sh
chmod +x build.sh
./build.sh
```

On `arm64`, use the cross toolchain explicitly:

```sh
CROSS_COMPILE=x86_64-linux-gnu- ./build.sh
```

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
qemu-system-x86_64 -cdrom out/adavalinux-bios.iso -m 1024M
```

### UEFI ISO

Create a writable OVMF variable file once:

```sh
cp /usr/share/OVMF/OVMF_VARS_4M.fd ./AdavaLinux_VARS.fd
```

Then boot the UEFI ISO:

```sh
qemu-system-x86_64 \
  -machine q35,accel=kvm \
  -cpu host \
  -m 1024 \
  -device virtio-vga \
  -display gtk \
  -serial stdio \
  -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
  -drive if=pflash,format=raw,file=./AdavaLinux_VARS.fd \
  -cdrom out/adavalinux-uefi.iso \
  -boot d
```

If KVM is not available, remove `,accel=kvm` and `-cpu host`.

## Install to a QEMU Disk

Create a qcow2 disk:

```sh
qemu-img create -f qcow2 disk.qcow2 2G
```

Boot the installer ISO with the target disk attached:

```sh
qemu-system-x86_64 \
  -m 1024M \
  -drive file=disk.qcow2,format=qcow2 \
  -cdrom out/adavalinux-bios.iso \
  -boot d
```

Inside AdavaLinux, run the installer:

```sh
# INSTALL_MEDIA is the device you booted the installer from.
# DISK is the destination disk.
INSTALL_MEDIA=/dev/sdb DISK=/dev/sda ./install.sh
```

After installation, boot from the disk image:

```sh
qemu-system-x86_64 -m 1024M -drive file=disk.qcow2,format=qcow2
```
