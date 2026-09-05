#!/bin/sh
set -eu
c=/usr/local/xscale_be/bin/xscale_be-gcc;s=/workspace/source;o=/workspace/build/4vrs-trm138-gateway-test
"$c" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I"$s/src" -o "$o" \
 "$s/tests/gateway/modbus_tcp_rtu_target.c" "$s/src/network/modbus_tcp_listener.c" \
 "$s/src/modbus/mbap_stream.c" "$s/src/modbus/modbus_crc.c" "$s/src/modbus/modbus_tcp_adapter.c" "$s/src/modbus/modbus_dispatcher.c" \
 "$s/src/core/deadline.c" "$s/src/core/port_runtime.c" "$s/src/uart/uart_backend.c" -lrt
ls -l "$o"
