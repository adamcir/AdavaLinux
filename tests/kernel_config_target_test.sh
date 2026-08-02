#!/usr/bin/env sh
set -eu

makefile="Makefile"

grep -Fq '"$$CFG_TOOL" --file "$$KERNEL_DIR/.config" --enable "$${line%%=*}"' "$makefile"
grep -Fq '"$$CFG_TOOL" --file "$$KERNEL_DIR/.config" --module "$${line%%=*}"' "$makefile"
grep -Fq '"$$CFG_TOOL" --file "$$KERNEL_DIR/.config" --disable "$$opt"' "$makefile"

printf '%s\n' 'kernel config target test passed'
