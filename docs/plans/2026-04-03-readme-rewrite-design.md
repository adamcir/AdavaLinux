# README Rewrite Design

**Goal:** Replace the placeholder README with a concise English guide for building, running, and installing AdavaLinux.

**Scope:** This change only updates repository documentation. It does not modify the build system, runtime behavior, or packaging flow.

## Chosen Approach

Use a practical GitHub-oriented structure instead of the current website-style HTML content. Keep only the information needed to get from source checkout to a bootable ISO and a QEMU test environment.

## Content Priorities

- Short project description
- Host requirements for `x86_64` and `arm64`
- Exact commands to fetch kernel and BusyBox sources
- Build instructions with `build.sh`
- QEMU run commands for BIOS and UEFI ISO images
- Minimal qcow2 installation workflow
- Output artifact summary
