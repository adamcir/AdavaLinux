#!/usr/bin/env sh
set -eu

config="filesforlinux/kernel.config"

for option in \
    'CONFIG_FB_EFI=y' \
    'CONFIG_FRAMEBUFFER_CONSOLE=y' \
    'CONFIG_DRM_SIMPLEDRM=y' \
    'CONFIG_DRM_BOCHS=y' \
    'CONFIG_DRM_VIRTIO_GPU=y'; do
    grep -Fxq "$option" "$config"
done

printf '%s\n' 'UEFI KMS kernel configuration test passed'
