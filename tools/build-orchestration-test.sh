#!/bin/sh
set -eu
c=/usr/local/xscale_be/bin/xscale_be-gcc;s=/workspace/source;o=/workspace/build/4vrs-orchestration-self-test
"$c" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I"$s/src" -I"$s/tests" -o "$o" \
 "$s/tests/gateway/test_gateway_orchestration.c" "$s/tests/gateway/gateway_orchestration_mock.c" \
 "$s/src/gateway/gateway_controller.c" "$s/src/config/config_model.c" \
 "$s/src/core/deadline.c" "$s/src/core/port_runtime.c" "$s/src/core/mock_backend.c"
ls -l "$o"
