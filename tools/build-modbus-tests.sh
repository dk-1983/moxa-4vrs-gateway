#!/bin/sh
set -eu

compiler=/usr/local/xscale_be/bin/xscale_be-gcc
source_root=/workspace/source
build_root=/workspace/build
output="$build_root/4vrs-modbus-tcp-self-test"

set -x
"$compiler" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian \
    -msoft-float -I"$source_root/src" -o "$output" \
    "$source_root/tests/modbus/test_modbus_tcp_adapter.c" \
    "$source_root/src/modbus/mbap_stream.c" \
    "$source_root/src/modbus/modbus_crc.c" \
    "$source_root/src/modbus/modbus_tcp_adapter.c" \
    "$source_root/src/modbus/modbus_dispatcher.c" \
    "$source_root/src/core/deadline.c" \
    "$source_root/src/core/port_runtime.c" \
    "$source_root/src/core/mock_backend.c"
set +x
ls -l "$output"
