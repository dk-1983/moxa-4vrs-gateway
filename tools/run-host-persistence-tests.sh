#!/bin/sh
set -eu
root=${1:-.}; out=${2:-build/host-persistence}; mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -o "$out/test-gateway-persistence" \
 "$root/tests/config/test_gateway_persistence.c" "$root/src/config/gateway_persistence.c" \
 "$root/src/gateway/gateway_controller.c" "$root/src/config/config_model.c" \
 "$root/src/core/deadline.c" "$root/src/core/port_runtime.c"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT INT TERM
"$out/test-gateway-persistence" "$tmp"
