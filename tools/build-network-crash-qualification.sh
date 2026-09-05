#!/bin/sh
set -eu
root=${1:?root}; out=${2:?output executable}
c=${CC:-/usr/local/xscale_be/bin/xscale_be-gcc}
flags=${CFLAGS:--std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float}
"$c" $flags -I"$root/src" -o "$out" "$root/tests/network/network_crash_qualification.c" \
 "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_boot.c" \
 "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" \
 "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" \
 "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" \
 "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" \
 "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" \
 "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" \
 "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/core/deadline.c" -Wl,--wrap=fsync,--wrap=unlink -lrt
