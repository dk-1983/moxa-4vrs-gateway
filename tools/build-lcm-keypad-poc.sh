#!/bin/sh
set -eu

compiler=/usr/local/xscale_be/bin/xscale_be-gcc
source_root=/workspace/source
build_root=/workspace/build
output="$build_root/4vrs-lcm-keypad-poc"

set -x
"$compiler" \
    -std=c89 \
    -Os \
    -Wall \
    -Wextra \
    -mcpu=xscale \
    -mbig-endian \
    -msoft-float \
    -I"$source_root/src" \
    -o "$output" \
    "$source_root/src/hardware_ui/lcm_keypad_poc.c"
set +x

ls -l "$output"
