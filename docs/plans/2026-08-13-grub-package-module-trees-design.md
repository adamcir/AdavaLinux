# GRUB package module trees design

## Goal

Remove the redundant top-level `/grub-install-modules` tree from the installer
initramfs and ISO.  The GRUB platform modules remain in the normal
`/usr/lib/grub/<platform>` location, supplied only by the `grub-bios` and
`grub-efi` SystemPackager packages.

## Flow

The ISO contains the two GRUB `.syspckg` archives in the existing local
package repository, but no extracted `grub-install-modules` directory.  At
the start of installation, the installer installs the selected GRUB package
into its RAM-backed live root.  It then resolves the module directory from
`/usr/lib/grub/i386-pc` or `/usr/lib/grub/x86_64-efi` when invoking
`grub-install`.

Later, it installs the same package into `/mnt/root`, where its modules live
only under the standard `/usr/lib/grub` package path.  No top-level temporary
module directory is copied into the target root, so there is nothing extra to
clean up after installation.

## Packaging

`../syspckg/grub-bios-2.12` must contain the i386-pc tree and
`../syspckg/grub-efi-2.12` must contain the x86_64-efi tree.  Their generated
`.syspckg` archives are the sole source of these trees on the ISO.

## Reliability

This preserves the previous optical-media workaround: `grub-install` reads
modules from the live RAM filesystem after the package has been installed,
not directly from the mounted ISO.  Failed package installation or an absent
platform directory remains a hard installation error with existing logging.

## Tests

Static regression tests will assert that the ISO/initramfs build no longer
copies `grub-install-modules`, that the resolver no longer considers that
path, and that it searches standard GRUB package locations.  Package-content
checks will assert each package includes its corresponding platform tree.
