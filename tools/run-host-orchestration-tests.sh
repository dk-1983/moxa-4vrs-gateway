#!/bin/sh
set -eu
root=${1:-.}; out=${2:-build/host-orchestration}; mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -I"$root/tests" \
 -o "$out/test-gateway-orchestration" \
 "$root/tests/gateway/test_gateway_orchestration.c" \
 "$root/tests/gateway/gateway_orchestration_mock.c" \
 "$root/src/gateway/gateway_controller.c" "$root/src/config/config_model.c" \
 "$root/src/core/deadline.c" "$root/src/core/port_runtime.c" "$root/src/core/mock_backend.c"
"$out/test-gateway-orchestration"
