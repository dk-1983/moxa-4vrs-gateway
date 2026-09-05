#!/bin/sh
set -eu
c=/usr/local/xscale_be/bin/xscale_be-gcc;s=/workspace/source;o=/workspace/build/4vrs-persistence-self-test
"$c" -std=c99 -Os -Wall -Wextra -mcpu=xscale -mbig-endian -msoft-float -I"$s/src" -o "$o" \
 "$s/tests/config/test_gateway_persistence.c" "$s/src/config/gateway_persistence.c" \
 "$s/src/gateway/gateway_controller.c" "$s/src/config/config_model.c" \
 "$s/src/core/deadline.c" "$s/src/core/port_runtime.c"
ls -l "$o"
