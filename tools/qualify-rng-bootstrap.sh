#!/bin/sh
# Run in the existing local, network-disabled XScale build container.
# SOURCE must be a Linux snapshot, not a Windows bind mount for GCC 3.4.3.
set -eu
source_root=$1
output_root=$2
host_helper=$3
ubsan_helper=$4
preserved_libs=$5
mkdir -p "$output_root"
gcc -std=gnu99 -O2 -Wall -Wextra -Wno-misleading-indentation -I"$source_root/src" -DWEB_HOST_TEST "$source_root/src/web/rng_bootstrap.c" -o "$output_root/receiver-host" -lrt
gcc -std=gnu99 -O1 -g -fsanitize=undefined -fno-sanitize-recover=all -I"$source_root/src" -DWEB_HOST_TEST "$source_root/src/web/rng_bootstrap.c" -o "$output_root/receiver-ubsan" -lrt
/usr/local/xscale_be/bin/xscale_be-gcc -Os -Wall -I"$source_root/src" "$source_root/src/web/rng_bootstrap.c" -o "$output_root/4vrs-rng-bootstrap" -lrt
python3 "$source_root/tests/web/test_rng_bootstrap_receiver.py" "$output_root/receiver-host" "$host_helper" > "$output_root/host.log" 2>&1
python3 "$source_root/tests/web/test_rng_bootstrap_receiver.py" "$output_root/receiver-ubsan" "$ubsan_helper" > "$output_root/ubsan.log" 2>&1
python3 "$source_root/tools/audit-web-abi.py" "$preserved_libs" "$output_root/abi.json" "$output_root/4vrs-rng-bootstrap"
sha256sum "$output_root/4vrs-rng-bootstrap"
tail -1 "$output_root/host.log"
tail -1 "$output_root/ubsan.log"
