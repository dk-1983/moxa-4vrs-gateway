#!/bin/sh
set -eu
root=${1:?root};out=${2:?out};mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -I"$root/tests" -o "$out/test-gateway-application-network" \
 "$root/tests/app/test_gateway_application_network.c" "$root/tests/gateway/gateway_orchestration_mock.c" \
 "$root/src/app/gateway_application.c" "$root/src/app/gateway_application_network.c" "$root/src/network/gateway_network_boot.c" "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" "$root/src/gateway/gateway_coordinator.c" \
 "$root/src/gateway/gateway_controller.c" "$root/src/gateway/gateway_real_adapter.c" \
 "$root/src/config/gateway_persistence.c" "$root/src/config/config_model.c" \
 "$root/src/network/modbus_tcp_listener.c" "$root/src/network/modbus_udp_listener.c" "$root/src/network/raw_serial_listener.c" "$root/src/modbus/modbus_dispatcher.c" \
 "$root/src/modbus/modbus_tcp_adapter.c" "$root/src/modbus/mbap_stream.c" \
 "$root/src/modbus/modbus_crc.c" \
 "$root/src/uart/uart_backend.c" "$root/src/core/deadline.c" \
 "$root/src/core/port_runtime.c" "$root/src/core/mock_backend.c" "$root/src/panel/gateway_panel.c" "$root/src/diagnostics/gateway_diagnostics.c" -Wl,--wrap=recv,--wrap=fsync -lrt
"$out/test-gateway-application-network"
LAN2_LEGACY_PEER=1 "$out/test-gateway-application-network"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -o "$out/test-active-provider" "$root/tests/network/test_gateway_network_active_provider.c" "$root/src/network/gateway_network_boot.c" "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" "$root/src/core/deadline.c" -Wl,--wrap=socket,--wrap=ioctl,--wrap=close -lrt
"$out/test-active-provider"
