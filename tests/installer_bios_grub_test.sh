#!/bin/sh
set -eu

installer=tools/installer/install.c

# grub2-install writes the MBR and embeds core.img itself.  A second direct
# grub2-bios-setup call rejects valid partition layouts when embedding is not
# available, so it must never be invoked by the installer.
grep -Fq '"--target=i386-pc"' "$installer"
grep -Fq '(char *)cfg->disk' "$installer"
if grep -Fq 'grub2-bios-setup' "$installer"; then
  echo 'installer must not invoke grub2-bios-setup' >&2
  exit 1
fi

printf '%s\n' 'BIOS GRUB installer test passed'
