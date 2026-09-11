#!/bin/sh
set -eu
source_root=${1:-.}
build_root=${2:-build/host-listener}
mkdir -p "$build_root"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror -D_DEFAULT_SOURCE} \
  -I"$source_root/src" -I"$source_root/tests" \
  -o "$build_root/test-modbus-tcp-listener" \
  "$source_root/tests/network/test_modbus_tcp_listener.c" \
  "$source_root/tests/network/echo_rtu_backend.c" \
  "$source_root/src/network/modbus_tcp_listener.c" \
  "$source_root/src/modbus/mbap_stream.c" "$source_root/src/modbus/modbus_crc.c" \
  "$source_root/src/modbus/modbus_tcp_adapter.c" "$source_root/src/modbus/modbus_dispatcher.c" \
  "$source_root/src/core/deadline.c" "$source_root/src/core/port_runtime.c"
"$build_root/test-modbus-tcp-listener"
