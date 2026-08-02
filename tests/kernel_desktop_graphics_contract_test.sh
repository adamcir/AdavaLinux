#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
config="$project_dir/filesforlinux/kernel.config"

for setting in \
  CONFIG_DRM=y \
  CONFIG_DRM_KMS_HELPER=y \
  CONFIG_DRM_SIMPLEDRM=y \
  CONFIG_DRM_VIRTIO_GPU=y \
  CONFIG_DRM_I915=y \
  CONFIG_DRM_AMDGPU=y \
  CONFIG_INPUT_EVDEV=y \
  CONFIG_VT=y; do
  grep -qx "$setting" "$config" || {
    printf 'missing desktop graphics setting: %s\n' "$setting" >&2
    exit 1
  }
done

printf 'kernel desktop graphics contract tests passed\n'
