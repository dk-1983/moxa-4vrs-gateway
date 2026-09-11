#!/bin/sh
set -eu

source_root=${1:-.}
build_root=${2:-build/host-core}
mkdir -p "$build_root"

cc ${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror} \
    -I"$source_root/src" \
    -o "$build_root/test-transport-core" \
    "$source_root/tests/core/test_transport_core.c" \
    "$source_root/src/core/deadline.c" \
    "$source_root/src/core/port_runtime.c" \
    "$source_root/src/core/mock_backend.c"

"$build_root/test-transport-core"
