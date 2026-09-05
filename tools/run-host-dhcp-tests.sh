#!/bin/sh
set -eu
root=${1:-.};out=${2:-build/host-dhcp}
mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/tests/network/test_gateway_dhcp_client.c" \
 -o "$out/test-dhcp-client"
"$out/test-dhcp-client"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" \
 "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" \
 "$root/src/network/gateway_network_owner.c" "$root/tests/network/test_gateway_network_owner.c" \
 -o "$out/test-network-owner"
"$out/test-network-owner"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" \
 "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_system.c" \
 "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" \
 "$root/src/network/gateway_network_supervisor.c" "$root/src/core/deadline.c" \
 "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" \
 "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_network_service.c" \
 "$root/tests/network/test_gateway_network_service.c" -o "$out/test-network-service" -lrt
"$out/test-network-service"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" \
 "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_system.c" \
 "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" \
 "$root/src/network/gateway_network_supervisor.c" "$root/src/core/deadline.c" \
 "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" \
 "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_network_service.c" \
 "$root/src/network/gateway_network_boot.c" "$root/tests/network/test_gateway_network_boot.c" -o "$out/test-network-boot" -Wl,--wrap=socket,--wrap=ioctl,--wrap=close -lrt
"$out/test-network-boot"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" \
 "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_system.c" \
 "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" \
 "$root/src/network/gateway_network_supervisor.c" "$root/src/core/deadline.c" \
 "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" \
 "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_network_service.c" \
 "$root/src/network/gateway_network_boot.c" "$root/tests/network/test_gateway_network_import_policy.c" -o "$out/test-network-import" -lrt
"$out/test-network-import"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" \
 "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_observation.c" \
 "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/tests/network/test_gateway_dhcp_io.c" \
 -o "$out/test-dhcp-io" -Wl,--wrap=socket,--wrap=ioctl,--wrap=fcntl,--wrap=setsockopt,--wrap=bind,--wrap=close,--wrap=recv,--wrap=sendto
"$out/test-dhcp-io"