#!/bin/sh
set -eu
compiler=/usr/local/xscale_be/bin/xscale_be-gcc
source_root=/workspace/source
output=/workspace/build/4vrs-modbus-listener-test
set -x
"$compiler" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float \
  -I"$source_root/src" -I"$source_root/tests" -o "$output" \
  "$source_root/tests/network/modbus_listener_target.c" \
  "$source_root/tests/network/echo_rtu_backend.c" \
  "$source_root/src/network/modbus_tcp_listener.c" \
  "$source_root/src/modbus/mbap_stream.c" "$source_root/src/modbus/modbus_crc.c" \
  "$source_root/src/modbus/modbus_tcp_adapter.c" "$source_root/src/modbus/modbus_dispatcher.c" \
  "$source_root/src/core/deadline.c" "$source_root/src/core/port_runtime.c" -lrt
set +x
ls -l "$output"
