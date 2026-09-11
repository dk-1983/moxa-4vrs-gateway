#!/bin/sh
set -eu
cc=/usr/local/xscale_be/bin/xscale_be-gcc
src=/workspace/source
out=/workspace/build
flags="-std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I$src/src"
set -x
$cc $flags "$src/tests/uart/test_uart_backend.c" "$src/src/uart/uart_backend.c" \
  "$src/src/core/deadline.c" "$src/src/core/port_runtime.c" \
  "$src/src/core/mock_backend.c" -o "$out/4vrs-uart-backend-test"
$cc $flags "$src/tests/uart/uart_hardware_lifecycle.c" \
  "$src/src/uart/uart_backend.c" "$src/src/core/deadline.c" \
  "$src/src/core/port_runtime.c" -o "$out/4vrs-uart-lifecycle-test"
$cc $flags "$src/tests/uart/uart_loopback_test.c" "$src/src/uart/uart_backend.c" \
  "$src/src/core/deadline.c" "$src/src/core/port_runtime.c" \
  "$src/src/core/mock_backend.c" -o "$out/4vrs-uart-loopback-test"
$cc $flags "$src/tests/uart/uart_rs485_p1_p2_test.c" "$src/src/uart/uart_backend.c" \
  "$src/src/core/deadline.c" "$src/src/core/port_runtime.c" \
  "$src/src/core/mock_backend.c" -o "$out/4vrs-uart-rs485-p1-p2-test"
$cc $flags "$src/tests/uart/uart_rs485_recovery_test.c" "$src/src/uart/uart_backend.c" \
  "$src/src/core/deadline.c" "$src/src/core/port_runtime.c" \
  "$src/src/core/mock_backend.c" -o "$out/4vrs-uart-rs485-recovery-test"
$cc $flags "$src/tests/uart/uart_rs485_anomaly_test.c" "$src/src/uart/uart_backend.c" \
  "$src/src/core/deadline.c" "$src/src/core/port_runtime.c" \
  "$src/tests/uart/uart_anomaly_control.c" -I"$src/tests/uart" \
  -o "$out/4vrs-uart-rs485-anomaly-test"
$cc $flags "$src/tests/uart/test_uart_anomaly_control.c" \
  "$src/tests/uart/uart_anomaly_control.c" -I"$src/tests/uart" \
  -o "$out/4vrs-uart-anomaly-control-test"
set +x
ls -l "$out/4vrs-uart-backend-test" "$out/4vrs-uart-lifecycle-test" \
  "$out/4vrs-uart-loopback-test"
ls -l "$out/4vrs-uart-rs485-p1-p2-test"
ls -l "$out/4vrs-uart-rs485-recovery-test"
ls -l "$out/4vrs-uart-rs485-anomaly-test"
ls -l "$out/4vrs-uart-anomaly-control-test"
