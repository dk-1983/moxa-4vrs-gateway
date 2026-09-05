#!/bin/sh
set -eu
root=${1:-.};out=${2:-build/host-network-settings}
mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" \
 "$root/tests/network/test_gateway_network_settings.c" -o "$out/test-network-settings"
"$out/test-network-settings"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" \
 "$root/src/network/gateway_network_document.c" \
 "$root/tests/network/test_gateway_network_document.c" -o "$out/test-network-document"
"$out/test-network-document"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_store.c" \
 "$root/tests/network/test_gateway_network_store.c" \
 -Wl,--wrap=fsync,--wrap=rename -o "$out/test-network-store"
"$out/test-network-store"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" \
 "$root/src/network/gateway_network_manager.c" "$root/src/core/deadline.c" \
 "$root/tests/network/test_gateway_network_manager.c" -o "$out/test-network-manager"
"$out/test-network-manager"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" \
 "$root/src/network/gateway_network_profile.c" "$root/tests/network/test_gateway_network_profile.c" \
 -o "$out/test-network-profile"
"$out/test-network-profile"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_store.c" \
 "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" \
 "$root/src/core/deadline.c" "$root/tests/network/test_gateway_network_supervisor.c" \
 -Wl,--wrap=recv -o "$out/test-network-supervisor" -lrt
"$out/test-network-supervisor"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_manager.c" \
 "$root/src/network/gateway_network_supervisor.c" "$root/src/core/deadline.c" \
 "$root/tests/network/test_gateway_network_peer_events.c" \
 -Wl,--wrap=poll,--wrap=recv,--wrap=send -o "$out/test-network-peer-events"
"$out/test-network-peer-events"
