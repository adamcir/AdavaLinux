# SystemPackager

SystemPackager is the AdavaLinux package manager.

**AdavaLinux package-manager version:** 1.0

The current implementation is a native C++ frontend linked directly to **libdnf5** and RPM.
It does not execute the external `dnf5` command.

## Package sources

- AdavaLinux RPM repository (`adavalinux`)
- Fedora repositories (`fedora`, `updates`)
- automatic mode, where both are available and repository priority decides

## CLI

```text
syspckg install <package>... [--source auto|adava|fedora] [-y]
syspckg remove <package>... [-y]
syspckg update [package]... [--source auto|adava|fedora] [-y]
syspckg upgrade [package]... [--source auto|adava|fedora] [-y]
syspckg search <term>... [--source auto|adava|fedora]
syspckg info <package>... [--source auto|adava|fedora]
syspckg list [installed] [--source auto|adava|fedora]
syspckg repos
syspckg clean
```

Package names always follow an explicit command, for example `syspckg install htop`.

## Manual

Use `man syspckg` on AdavaLinux.

## Legacy System/1 package manager

The former native `.syspckg` implementation is kept only for historical System/1
source compatibility under `tools/syspckg-old`.

It is **unsupported**, is not built by the normal AdavaLinux build, and is not copied
into the current root filesystem.
