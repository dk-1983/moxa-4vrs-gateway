#!/bin/sh
set -eu
c=${CROSS_CC:-/usr/local/xscale_be/bin/xscale_be-gcc};root=${1:-/workspace/source};out=${2:-/workspace/build/4vrs-gateway}
"$c" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I"$root/src" -o "$out" \
 "$root/src/app/main.c" "$root/src/network/gateway_network_boot.c" "$root/src/app/gateway_application.c" "$root/src/app/gateway_application_network.c" "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/console/gateway_console.c" "$root/src/diagnostics/gateway_diagnostics.c" \
 "$root/src/launcher/gateway_startup_presentation.c" \
 "$root/src/panel/gateway_panel.c" "$root/src/panel/key_repeat.c" "$root/src/panel/gateway_panel_moxa.c" \
 "$root/src/gateway/gateway_coordinator.c" "$root/src/gateway/gateway_controller.c" \
 "$root/src/gateway/gateway_real_adapter.c" "$root/src/config/gateway_persistence.c" \
 "$root/src/config/config_model.c" "$root/src/network/modbus_tcp_listener.c" "$root/src/network/modbus_udp_listener.c" "$root/src/network/raw_serial_listener.c" \
 "$root/src/modbus/modbus_dispatcher.c" "$root/src/modbus/modbus_tcp_adapter.c" \
 "$root/src/modbus/mbap_stream.c" "$root/src/uart/uart_backend.c" \
 "$root/src/modbus/modbus_crc.c" \
 "$root/src/core/deadline.c" "$root/src/core/port_runtime.c" -lrt
ls -l "$out"
