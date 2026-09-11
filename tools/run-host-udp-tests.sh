#!/bin/sh
set -eu
root=${1:-.}; out=${2:-build/host-udp}; mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -I"$root/tests" \
 -o "$out/test-modbus-udp" "$root/tests/network/test_modbus_udp_listener.c" \
 "$root/tests/network/echo_rtu_backend.c" "$root/src/network/modbus_udp_listener.c" \
 "$root/src/modbus/modbus_tcp_adapter.c" "$root/src/modbus/modbus_crc.c" \
 "$root/src/core/port_runtime.c" "$root/src/core/deadline.c"
"$out/test-modbus-udp"
