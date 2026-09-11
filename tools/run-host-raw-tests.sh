#!/bin/sh
set -eu
root=${1:-.}; out=${2:-build/host-raw}; mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 -o "$out/test-raw-serial" "$root/tests/network/test_raw_serial_listener.c" \
 "$root/src/network/raw_serial_listener.c" "$root/src/core/port_runtime.c" "$root/src/core/deadline.c"
"$out/test-raw-serial"
