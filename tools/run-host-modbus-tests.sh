#!/bin/sh
set -eu

source_root=${1:-.}
build_root=${2:-build/host-modbus}
mkdir -p "$build_root"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} \
    -I"$source_root/src" \
    -o "$build_root/test-modbus-tcp-adapter" \
    "$source_root/tests/modbus/test_modbus_tcp_adapter.c" \
    "$source_root/src/modbus/mbap_stream.c" \
    "$source_root/src/modbus/modbus_crc.c" \
    "$source_root/src/modbus/modbus_tcp_adapter.c" \
    "$source_root/src/modbus/modbus_dispatcher.c" \
    "$source_root/src/core/deadline.c" \
    "$source_root/src/core/port_runtime.c" \
    "$source_root/src/core/mock_backend.c"

"$build_root/test-modbus-tcp-adapter"
