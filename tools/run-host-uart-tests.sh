#!/bin/sh
set -eu
source_root=${1:-.}
build_root=${2:-build/host-uart}
mkdir -p "$build_root"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$source_root/src" \
  "$source_root/tests/uart/test_uart_backend.c" \
  "$source_root/src/uart/uart_backend.c" "$source_root/src/core/deadline.c" \
  "$source_root/src/core/port_runtime.c" "$source_root/src/core/mock_backend.c" \
  -o "$build_root/test-uart-backend"
"$build_root/test-uart-backend"
cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} -I"$source_root/tests/uart" \
  "$source_root/tests/uart/test_uart_anomaly_control.c" \
  "$source_root/tests/uart/uart_anomaly_control.c" \
  -o "$build_root/test-uart-anomaly-control"
"$build_root/test-uart-anomaly-control"
