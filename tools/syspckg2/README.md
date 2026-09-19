# SystemPackager 2

SystemPackager 2 is the RPM/DNF5 generation of the AdavaLinux package manager.

The original `tools/syspckg` remains available for legacy `.syspckg`
packages. New packages are expected to use RPM and are handled through
`syspckg2`.

## Sources

SystemPackager 2 uses two RPM sources:

- **AdavaLinux** — AdavaLinux-specific and Adava Software packages.
- **Fedora 44** — upstream RPM packages such as Xfce, GTK, GCC, GIMP and
  their dependencies.

The AdavaLinux repository has a lower numeric DNF priority and therefore wins
when the same package is available from both sources.

## Usage

```sh
syspckg2 xfce4
syspckg2 install gimp
syspckg2 install adaterm --adava
syspckg2 install xfce4 --fedora
syspckg2 search terminal
syspckg2 info xfce4
syspckg2 update
syspckg2 repos
```

`syspckg2 <package>` is a shortcut for `syspckg2 install <package>`.

## Backend

This first transition version uses DNF5 as the RPM transaction engine. It does
not use the legacy SystemPackager database or `.syspckg` format.

The frontend deliberately has its own AdavaLinux CLI and repository policy.
A later revision can switch the implementation from executing `dnf5` to
calling `libdnf5` directly without changing the command-line interface.

Set `SYSPCKG2_BACKEND` to override the backend executable during development
or tests.

## Repository configuration

Vendor repository files are installed under
`/usr/share/dnf5/repos.d/`.

Fedora is pinned to Fedora 44 for the AdavaLinux 2 transition instead of using
AdavaLinux's own VERSION value as DNF's `$releasever`.
