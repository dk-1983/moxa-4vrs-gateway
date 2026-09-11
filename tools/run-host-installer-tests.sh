#!/bin/sh
set -eu
root=${1:?source root required}
out=${2:?separate output directory required}
mkdir -p "$out"
${CC:-cc} ${CFLAGS:--std=c99 -O2 -g -Wall -Wextra -Werror} -I"$root/src" \
 "$root/tests/installer/test_transaction.c" \
 "$root/src/installer/install_transaction.c" \
 "$root/src/installer/install_files.c" "$root/src/installer/install_digest.c" \
 -Wl,--wrap=fsync -o "$out/test-transaction"
"$out/test-transaction" "$out"
${CC:-cc} ${CFLAGS:--std=c99 -O2 -g -Wall -Wextra -Werror} -I"$root/src" \
 "$root/tests/installer/package_probe.c" "$root/src/installer/install_package.c" \
 "$root/src/installer/install_files.c" "$root/src/installer/install_digest.c" \
 -o "$out/package-probe"
python3 "$root/tests/installer/test_native_package.py" "$root" "$out" "$out/package-probe"
${CC:-cc} ${CFLAGS:--std=c99 -O2 -g -Wall -Wextra -Werror} -I"$root/src" \
 "$root/tests/installer/test_scripts.c" "$root/src/installer/install_scripts.c" \
 "$root/src/installer/install_files.c" -o "$out/test-scripts"
"$out/test-scripts"
${CC:-cc} ${CFLAGS:--std=c99 -O2 -g -Wall -Wextra -Werror} -I"$root/src" \
 "$root/tests/installer/test_process.c" "$root/src/installer/install_process.c" \
 -o "$out/test-process" -lrt
"$out/test-process" "$out"
python3 "$root/tests/installer/run_network.py" "$root" "$out"

python3 "$root/tests/installer/run_orchestrator.py" "$root" "$out"
