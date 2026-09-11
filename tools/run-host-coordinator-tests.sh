#!/bin/sh
set -eu
root=${1:-.}; out=${2:-build/host-coordinator}; mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -I"$root/tests" -o "$out/test-gateway-coordinator" \
 "$root/tests/gateway/test_gateway_coordinator.c" "$root/tests/gateway/gateway_orchestration_mock.c" \
 "$root/src/gateway/gateway_coordinator.c" "$root/src/gateway/gateway_controller.c" \
 "$root/src/config/gateway_persistence.c" "$root/src/config/config_model.c" \
 "$root/src/core/deadline.c" "$root/src/core/port_runtime.c" "$root/src/core/mock_backend.c"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT INT TERM
"$out/test-gateway-coordinator" "$tmp"
