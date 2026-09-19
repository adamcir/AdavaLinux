# SystemPackager 2

SystemPackager 2 is the RPM/libdnf5 generation of the AdavaLinux package manager.

The original `tools/syspckg` remains available for legacy `.syspckg`
packages. New packages use RPM and are handled by `syspckg2`.

## Architecture

`syspckg2` is a native C++ frontend that links directly to **libdnf5**.
It does **not** execute the external `dnf5` command.

```text
SystemPackager 2
      |
      +-- libdnf5
            |
            +-- RPM
            +-- libsolv
            +-- librepo
            +-- Fedora repositories
            +-- AdavaLinux repository
```

## Sources

- **AdavaLinux** — AdavaLinux-specific and Adava Software RPM packages.
- **Fedora 44** — upstream RPM packages and dependencies.

AdavaLinux has the higher repository priority when both repositories provide
the same package.

## Usage

```sh
syspckg2 xfce4
syspckg2 install mc
syspckg2 install adaterm --adava
syspckg2 install xfce4 --fedora
syspckg2 remove mc
syspckg2 update
syspckg2 search terminal
syspckg2 info xfce4
syspckg2 list installed
syspckg2 repos
```

`syspckg2 <package>` is a shortcut for `syspckg2 install <package>`.

## Build

The top-level AdavaLinux Makefile prepares an amd64 libdnf5 sysroot under
`.toolcache/libdnf5-amd64`. On arm64 build hosts it is then cross-linked with
`x86_64-linux-gnu-g++`.

The sysroot is populated from an isolated Debian forky amd64 repository because
older Debian/Raspberry Pi OS releases do not provide libdnf5-dev. The temporary
APT state lives entirely inside `.toolcache`; no Debian testing repository is
added to the host system. Package repositories used by AdavaLinux itself remain
AdavaLinux and Fedora.

The ISO build copies the required shared libraries and RPM runtime data into
the AdavaLinux rootfs. No `/usr/bin/dnf5` executable is required.


## Repository failures

SystemPackager 2 marks enabled repositories as skip-if-unavailable while loading metadata.
If one repository cannot be fetched, it prints a warning and continues with the
remaining repositories. If none of the enabled repositories can provide package
metadata, the command fails.
