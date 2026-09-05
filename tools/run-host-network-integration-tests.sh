#!/bin/sh
set -eu
root=${1:-.};out=${2:-build/host-network-integration}
mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/tests/network/test_gateway_network_observation.c" -o "$out/test-network-observation"
"$out/test-network-observation"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/tests/network/test_gateway_network_observe_provider.c" \
 -Wl,--wrap=open,--wrap=close,--wrap=socket,--wrap=read,--wrap=ioctl \
 -o "$out/test-network-observe-provider"
"$out/test-network-observe-provider"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_system.c" \
 "$root/tests/network/test_gateway_network_system.c" \
 -Wl,--wrap=socket,--wrap=ioctl,--wrap=close,--wrap=fsync -o "$out/test-network-system"
"$out/test-network-system"
