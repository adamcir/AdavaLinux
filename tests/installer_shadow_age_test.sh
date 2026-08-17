#!/bin/sh
set -eu

source=tools/installer/install.c

grep -Fq '#include <time.h>' "$source"
grep -Fq 'long password_change_day = (long)(time(NULL) / 86400);' "$source"
grep -Fq 'root:%s:%ld:0:99999:7:::' "$source"

printf '%s\n' 'installer shadow age test passed'
