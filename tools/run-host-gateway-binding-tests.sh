#!/bin/sh
set -eu
root=${1:-.};out=${2:-build/host-gateway-binding};mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -I"$root/tests" -o "$out/test-gateway-real-adapter" \
 "$root/tests/gateway/test_gateway_real_adapter.c" \
 "$root/src/gateway/gateway_real_adapter.c" "$root/src/gateway/gateway_controller.c" \
 "$root/src/network/modbus_tcp_listener.c" "$root/src/network/modbus_udp_listener.c" "$root/src/network/raw_serial_listener.c" "$root/src/modbus/mbap_stream.c" \
 "$root/src/modbus/modbus_crc.c" "$root/src/modbus/modbus_tcp_adapter.c" \
 "$root/src/modbus/modbus_dispatcher.c" "$root/src/uart/uart_backend.c" \
 "$root/src/config/config_model.c" "$root/src/core/deadline.c" "$root/src/core/port_runtime.c"
"$out/test-gateway-real-adapter"
