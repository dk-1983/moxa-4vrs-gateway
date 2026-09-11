#!/bin/sh
set -eu

compiler=/usr/local/xscale_be/bin/xscale_be-gcc
source_root=/workspace/source
build_root=/workspace/build
output="$build_root/4vrs-ui-prototype"

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
    "$source_root/src/ui/ui_prototype.c" \
    "$source_root/src/ui/lcm_device.c" \
    "$source_root/src/ui/keypad_device.c" \
    "$source_root/src/config/config_model.c" \
    "$source_root/src/status/system_status.c"
set +x

ls -l "$output"
