#!/bin/sh
set -eu
c=/usr/local/xscale_be/bin/xscale_be-gcc;s=${1:-/workspace/source};o=${2:-/workspace/build/4vrs-panel-self-test}
"$c" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I"$s/src" -I"$s/tests" -o "$o" \
 "$s/tests/panel/test_gateway_panel.c" "$s/src/panel/gateway_panel.c" "$s/src/panel/key_repeat.c" \
 "$s/src/diagnostics/gateway_diagnostics.c" "$s/src/app/gateway_application.c" "$s/src/app/gateway_application_network.c" "$s/src/network/gateway_network_runtime.c" "$s/src/network/gateway_network_settings.c" "$s/src/network/gateway_network_document.c" "$s/src/network/gateway_network_profile.c" "$s/src/network/gateway_network_store.c" "$s/src/network/gateway_network_manager.c" "$s/src/network/gateway_network_supervisor.c" "$s/src/network/gateway_network_observation.c" \
 "$s/src/gateway/gateway_coordinator.c" "$s/src/gateway/gateway_controller.c" \
 "$s/src/gateway/gateway_real_adapter.c" "$s/src/config/gateway_persistence.c" \
 "$s/src/config/config_model.c" "$s/src/network/modbus_tcp_listener.c" "$s/src/network/modbus_udp_listener.c" "$s/src/network/raw_serial_listener.c" \
 "$s/src/modbus/modbus_dispatcher.c" "$s/src/modbus/modbus_tcp_adapter.c" \
 "$s/src/modbus/mbap_stream.c" "$s/src/modbus/modbus_crc.c" "$s/src/uart/uart_backend.c" \
 "$s/src/core/deadline.c" "$s/src/core/port_runtime.c" -lrt
ls -l "$o"
"/usr/local/xscale_be/bin/xscale_be-nm" -S "$o" | grep '_layout$'
