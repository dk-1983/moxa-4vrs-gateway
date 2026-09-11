#!/bin/sh
set -eu
root=${1:-.};out=${2:-build/host-launcher};mkdir -p "$out"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$root/src" -o "$out/test-gateway-launcher" \
 "$root/tests/launcher/test_gateway_launcher.c" "$root/src/launcher/gateway_startup_presentation.c" "$root/src/launcher/gateway_autostart.c" \
 "$root/src/app/gateway_application.c" "$root/src/app/gateway_application_network.c" "$root/src/network/gateway_network_runtime.c" "$root/src/network/gateway_network_import_policy.c" "$root/src/network/gateway_network_service.c" "$root/src/network/gateway_network_owner.c" "$root/src/network/gateway_dhcp_client.c" "$root/src/network/gateway_dhcp_wire.c" "$root/src/network/gateway_dhcp_io.c" "$root/src/network/gateway_network_system.c" "$root/src/network/gateway_network_settings.c" "$root/src/network/gateway_network_document.c" "$root/src/network/gateway_network_profile.c" "$root/src/network/gateway_network_store.c" "$root/src/network/gateway_network_manager.c" "$root/src/network/gateway_network_supervisor.c" "$root/src/network/gateway_network_observation.c" "$root/src/gateway/gateway_coordinator.c" "$root/src/gateway/gateway_controller.c" \
 "$root/src/gateway/gateway_real_adapter.c" "$root/src/config/gateway_persistence.c" "$root/src/config/config_model.c" \
 "$root/src/network/modbus_tcp_listener.c" "$root/src/network/modbus_udp_listener.c" "$root/src/network/raw_serial_listener.c" "$root/src/modbus/modbus_dispatcher.c" "$root/src/modbus/modbus_tcp_adapter.c" \
 "$root/src/modbus/mbap_stream.c" "$root/src/modbus/modbus_crc.c" "$root/src/uart/uart_backend.c" \
 "$root/src/core/deadline.c" "$root/src/core/port_runtime.c" -lrt
"$out/test-gateway-launcher"
sh -n "$root/deploy/4vrs-gateway.init"
grep -q 'disable-autostart' "$root/deploy/4vrs-gateway.init"
grep -q 'kill -TERM' "$root/deploy/4vrs-gateway.init"
grep -q 'test "/proc/\$1/exe" -ef "\$binary"' "$root/deploy/4vrs-gateway.init"
! grep -q 'readlink\|kill -KILL\|kill -9' "$root/deploy/4vrs-gateway.init"

fixture=$(mktemp -d)
helper_pid=
victim_pid=
trap 'test -z "$helper_pid" || kill "$helper_pid" 2>/dev/null || true; test -z "$victim_pid" || kill "$victim_pid" 2>/dev/null || true; rm -rf "$fixture"' EXIT INT TERM
mkdir -p "$fixture/root/bin" "$fixture/root/run" "$fixture/root/log"
sed "s#^root=/var/hda/4vrs\$#root=$fixture/root#" "$root/deploy/4vrs-gateway.init" >"$fixture/init"
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$fixture/root/bin/4vrs-gateway" -xc - <<'EOF'
#include <signal.h>
#include <unistd.h>
static volatile sig_atomic_t stop_requested;
static void stop(int signal_number){(void)signal_number;stop_requested=1;}
int main(void){signal(SIGTERM,stop);while(!stop_requested)pause();return 0;}
EOF
chmod 755 "$fixture/init" "$fixture/root/bin/4vrs-gateway"

"$fixture/root/bin/4vrs-gateway" & helper_pid=$!
printf '%s\n' "$helper_pid" >"$fixture/root/run/4vrs-gateway.pid"
"$fixture/init" start
test "$(cat "$fixture/root/run/4vrs-gateway.pid")" = "$helper_pid"
kill -0 "$helper_pid"
"$fixture/init" stop
wait "$helper_pid"
helper_pid=
test ! -e "$fixture/root/run/4vrs-gateway.pid"

printf '999999\n' >"$fixture/root/run/4vrs-gateway.pid"
"$fixture/init" start
helper_pid=$(cat "$fixture/root/run/4vrs-gateway.pid")
test "$helper_pid" != 999999
kill -0 "$helper_pid"
"$fixture/init" stop
if kill -0 "$helper_pid" 2>/dev/null; then exit 1; fi
helper_pid=

sleep 60 & victim_pid=$!
printf '%s\n' "$victim_pid" >"$fixture/root/run/4vrs-gateway.pid"
"$fixture/init" stop
kill -0 "$victim_pid"
test ! -e "$fixture/root/run/4vrs-gateway.pid"
kill "$victim_pid"; wait "$victim_pid" 2>/dev/null || true; victim_pid=

for stale in 999999 '' abc '12x' '-1' '1/../1'; do
 printf '%s\n' "$stale" >"$fixture/root/run/4vrs-gateway.pid"
 "$fixture/init" stop
 test ! -e "$fixture/root/run/4vrs-gateway.pid"
done

touch "$fixture/root/disable-autostart"
"$fixture/init" start
test ! -e "$fixture/root/run/4vrs-gateway.pid"
rm -f "$fixture/root/disable-autostart"
chmod 644 "$fixture/root/bin/4vrs-gateway"
if "$fixture/init" start >/dev/null 2>&1; then exit 1; fi
chmod 755 "$fixture/root/bin/4vrs-gateway"
rm -f "$fixture/root/bin/4vrs-gateway"
if "$fixture/init" start >/dev/null 2>&1; then exit 1; fi

printf 'launcher init syntax/recovery checks=25 failed=0\n'
