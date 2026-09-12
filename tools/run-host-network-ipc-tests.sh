#!/bin/sh
set -eu
root=${1:?source root required}
out=${2:?output directory required}
mkdir -p "$out"
for mode in normal ubsan; do
  flags=
  if test "$mode" = ubsan; then flags='-fsanitize=undefined -fno-sanitize-recover=all'; fi
  cc -std=c99 -O2 -Wall -Wextra -Werror $flags -DFOURVRS_NETWORK_STREAM_IPC=1 \
    -I"$root/src" "$root/tests/network/test_gateway_network_frames.c" -o "$out/frames-$mode"
  "$out/frames-$mode"
done
# Exercise process recovery and protocol handling with the same fixtures used
# for the default transport. Interface naming stays native to the host tests.
CFLAGS='-std=c99 -O2 -Wall -Wextra -Werror -DFOURVRS_NETWORK_STREAM_IPC=1' \
  sh "$root/tools/run-host-dhcp-tests.sh" "$root" "$out/stream"
