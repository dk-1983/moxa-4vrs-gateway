#!/bin/sh
# Local Docker fixtures only. Does not start init, getty or access hardware.
set -eu
source_root=$1
output_root=$2
vendor_init_sources=$3
receiver_host=$4
receiver_ubsan=$5
receiver_target=$6
preserved_libs=$7
mkdir -p "$output_root"
gcc -std=gnu99 -O2 -I"$source_root/src" "$source_root/tests/web/test_bootstrap_console_audit.c" -o "$output_root/audit-host" -lrt
gcc -std=gnu99 -O1 -fsanitize=undefined -fno-sanitize-recover=all -I"$source_root/src" "$source_root/tests/web/test_bootstrap_console_audit.c" -o "$output_root/audit-ubsan" -lrt
"$output_root/audit-host" > "$output_root/audit-host.log"
"$output_root/audit-ubsan" > "$output_root/audit-ubsan.log"
python3 "$source_root/tools/test-vendor-console-init.py" "$vendor_init_sources" "$output_root/init"
python3 "$source_root/tests/web/test_bootstrap_uart_exclusion.py" "$receiver_host" > "$output_root/uart-host.log"
python3 "$source_root/tests/web/test_bootstrap_uart_exclusion.py" "$receiver_ubsan" > "$output_root/uart-ubsan.log"
python3 "$source_root/tests/web/test_bootstrap_hold.py" > "$output_root/hold.log"
python3 "$source_root/tests/web/test_bootstrap_tty_binding.py" > "$output_root/tty-binding.log"
python3 "$source_root/tools/audit-web-abi.py" "$preserved_libs" "$output_root/abi.json" "$receiver_target"
tail -1 "$output_root/audit-host.log"
tail -1 "$output_root/audit-ubsan.log"
tail -1 "$output_root/uart-host.log"
tail -1 "$output_root/uart-ubsan.log"
tail -1 "$output_root/hold.log"
tail -1 "$output_root/tty-binding.log"
