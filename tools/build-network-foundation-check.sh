#!/bin/sh
# Compile all network foundation components with the original target ABI.
# The entry point exercises only in-memory profile parsing; it does not apply
# networking or prove product integration or hardware acceptance.
set -eu
root=${1:-/workspace/source}
out=${2:-/workspace/build/4vrs-network-foundation-check}
c=${CROSS_CC:-/usr/local/xscale_be/bin/xscale_be-gcc}
"$c" --version
"$c" -dumpmachine
"$c" -std=c99 -Os -Wall -Wextra -Werror -mcpu=xscale -mbig-endian -msoft-float \
 -I"$root/src" "$root/tests/network/test_gateway_network_profile.c" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" \
 "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" \
 "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" \
 "$root/src/core/deadline.c" -o "$out" -lrt
ls -l "$out"
