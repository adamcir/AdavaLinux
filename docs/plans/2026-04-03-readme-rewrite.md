# README Rewrite Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the README with a concise English setup guide for building and running AdavaLinux from source.

**Architecture:** Keep the README focused on reproducible command-line workflows. Prefer the exact commands already supported by `build.sh` and the current QEMU boot flow rather than broader explanations.

**Tech Stack:** Markdown, shell commands, existing build flow

---

### Task 1: Draft the new README structure

**Files:**
- Modify: `README.md`

**Step 1: Write the replacement content**

Include:

- Short introduction
- `x86_64` host dependencies
- `arm64` host dependencies
- Source download commands
- Build command
- QEMU BIOS and UEFI run commands
- qcow2 install workflow
- Output artifact list

### Task 2: Verify accuracy against the current build flow

**Files:**
- Modify: `README.md`
- Reference: `build.sh`

**Step 1: Match names and outputs to the build script**

Ensure the README uses the current artifact names:

- `out/adavalinux-bios.iso`
- `out/adavalinux-uefi.iso`
- `out/vmlinuz-6.19.11`
- `out/initramfs-installer.gz`
- `out/initramfs-disk.gz`

### Task 3: Final review

**Files:**
- Modify: `README.md`

**Step 1: Keep the document concise**

Remove website wording, duplicated explanations, and HTML-specific structure so the README reads cleanly on GitHub.
