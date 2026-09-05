#!/bin/sh
set -eu
c=/usr/local/xscale_be/bin/xscale_be-gcc;s=/workspace/source;o=/workspace/build/4vrs-orchestrated-p1-test
"$c" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I"$s/src" -o "$o" \
 "$s/tests/gateway/orchestrated_p1_target.c" "$s/src/gateway/gateway_real_adapter.c" \
 "$s/src/gateway/gateway_controller.c" "$s/src/network/modbus_tcp_listener.c" "$s/src/network/modbus_udp_listener.c" "$s/src/network/raw_serial_listener.c" \
 "$s/src/modbus/mbap_stream.c" "$s/src/modbus/modbus_crc.c" \
 "$s/src/modbus/modbus_tcp_adapter.c" "$s/src/modbus/modbus_dispatcher.c" \
 "$s/src/uart/uart_backend.c" "$s/src/config/config_model.c" \
 "$s/src/core/deadline.c" "$s/src/core/port_runtime.c" -lrt
ls -l "$o"
