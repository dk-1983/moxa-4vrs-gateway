#!/bin/sh
set -eu

compiler=/usr/local/xscale_be/bin/xscale_be-gcc
source_root=/workspace/source
build_root=/workspace/build
output="$build_root/4vrs-transport-core-test"

set -x
"$compiler" \
    -std=c99 \
    -Os \
    -Wall \
    -Wextra \
    -mcpu=xscale \
    -mbig-endian \
    -msoft-float \
    -I"$source_root/src" \
    -o "$output" \
    "$source_root/tests/core/test_transport_core.c" \
    "$source_root/src/core/deadline.c" \
    "$source_root/src/core/port_runtime.c" \
    "$source_root/src/core/mock_backend.c"
set +x

ls -l "$output"
