#!/bin/sh
set -eu
root=${1:-.};out=${2:-build/host-application};mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -I"$root/tests" -o "$out/test-gateway-application" \
 "$root/tests/app/test_gateway_application.c" "$root/tests/gateway/gateway_orchestration_mock.c" \
 "$root/src/app/gateway_application.c" "$root/src/app/gateway_application_network.c" "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" "$root/src/gateway/gateway_coordinator.c" \
 "$root/src/gateway/gateway_controller.c" "$root/src/gateway/gateway_real_adapter.c" \
 "$root/src/config/gateway_persistence.c" "$root/src/config/config_model.c" \
 "$root/src/network/modbus_tcp_listener.c" "$root/src/network/modbus_udp_listener.c" "$root/src/network/raw_serial_listener.c" "$root/src/modbus/modbus_dispatcher.c" \
 "$root/src/modbus/modbus_tcp_adapter.c" "$root/src/modbus/mbap_stream.c" \
 "$root/src/modbus/modbus_crc.c" \
 "$root/src/uart/uart_backend.c" "$root/src/core/deadline.c" \
 "$root/src/core/port_runtime.c" "$root/src/core/mock_backend.c" -lrt
"$out/test-gateway-application"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -o "$out/4vrs-gateway" \
 "$root/src/app/main.c" "$root/src/network/gateway_network_boot.c" "$root/src/app/gateway_application.c" "$root/src/app/gateway_application_network.c" "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/console/gateway_console.c" "$root/src/diagnostics/gateway_diagnostics.c" \
 "$root/src/launcher/gateway_startup_presentation.c" \
 "$root/src/panel/gateway_panel.c" "$root/src/panel/gateway_panel_moxa.c" \
 "$root/src/gateway/gateway_coordinator.c" "$root/src/gateway/gateway_controller.c" \
 "$root/src/gateway/gateway_real_adapter.c" "$root/src/config/gateway_persistence.c" \
 "$root/src/config/config_model.c" "$root/src/network/modbus_tcp_listener.c" "$root/src/network/modbus_udp_listener.c" "$root/src/network/raw_serial_listener.c" \
 "$root/src/modbus/modbus_dispatcher.c" "$root/src/modbus/modbus_tcp_adapter.c" \
 "$root/src/modbus/mbap_stream.c" "$root/src/modbus/modbus_crc.c" \
 "$root/src/uart/uart_backend.c" "$root/src/core/deadline.c" \
 "$root/src/core/port_runtime.c" -lrt
tmp=$(mktemp -d);pid=;trap 'test -z "$pid" || kill "$pid" 2>/dev/null || true;rm -rf "$tmp"' EXIT INT TERM
printf 'corrupt\n' >"$tmp/gateway.conf";printf 'corrupt\n' >"$tmp/gateway.conf.good"
"$out/4vrs-gateway" "$tmp" >"$tmp/output" 2>&1 & pid=$!
i=0;while ! grep -q 'startup_result=SAFE_MODE' "$tmp/output"&&test "$i" -lt 100;do i=$((i+1));sleep 0.01;done
grep -q 'product=4VRS Gateway version=v2026.01.01 state=starting' "$tmp/output"
grep -q 'configuration_source=safe-mode startup_result=SAFE_MODE' "$tmp/output"
kill -TERM "$pid";set +e;wait "$pid";status=$?;set -e;pid=
test "$status" -eq 10
printf 'application signal smoke: SAFE_MODE -> SIGTERM -> exit=%u\n' "$status"

# Failure diagnostics identify a stage, never disclose input paths/configuration.
set +e
"$out/4vrs-gateway" --network-enroll "$tmp/private-missing-config" >"$out/enroll-diagnostic.log" 2>&1
status=$?
set -e
test "$status" -eq 1
test "$(cat "$out/enroll-diagnostic.log")" = 'network-enroll failed stage=configuration'
printf 'enroll CLI diagnostic: exit=1 stage=configuration, no input disclosure\n'

# In this isolated host container the production store is absent. Boot must
# fail before any network provider is invoked, with a bounded stage only.
set +e
"$out/4vrs-gateway" --network-boot >"$out/boot-diagnostic.log" 2>&1
status=$?
set -e
test "$status" -eq 1
test "$(cat "$out/boot-diagnostic.log")" = 'network-boot failed stage=store-lock'
printf 'boot CLI diagnostic: exit=1 stage=store-lock, no configuration disclosure\n'
