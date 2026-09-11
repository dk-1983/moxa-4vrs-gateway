#!/bin/sh
set -eu
root=${1:?source root}
out=${2:?unique output ELF}
${CC:-cc} ${CFLAGS:--std=c99 -Os -Wall -Wextra -Werror} "$root/tools/qualification-observer.c" -lrt -o "$out"
