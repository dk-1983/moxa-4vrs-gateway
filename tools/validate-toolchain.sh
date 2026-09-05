#!/bin/sh
set -u

toolroot=/usr/local/xscale_be
gcc="$toolroot/bin/xscale_be-gcc"
as="$toolroot/bin/xscale_be-as"
ld="$toolroot/bin/xscale_be-ld"
gcclib="$toolroot/lib/gcc/armv5teb-montavista-linuxeabi/3.4.3"

run() {
    echo "===== $* ====="
    "$@"
    code=$?
    echo "exit=$code"
    return 0
}

inspect_host_elf() {
    executable=$1
    echo "===== host ELF: $executable ====="
    file "$executable"
    readelf -h "$executable" | grep -E 'Class:|Data:|Machine:'
    readelf -l "$executable" | grep 'Requesting program interpreter'
    readelf -d "$executable" | grep NEEDED || true
    readelf -V "$executable" | grep 'Name: GLIBC_' | sort -u || true
}

run "$gcc" --version
run "$gcc" -dumpmachine
run "$gcc" -print-sysroot
run "$gcc" -print-search-dirs
run "$gcc" -print-file-name=crt1.o
run "$gcc" -print-file-name=crti.o
run "$gcc" -print-file-name=crtn.o
run "$gcc" -print-file-name=libgcc.a
run "$gcc" -print-file-name=libgcc_s.so.1
run "$gcc" -print-file-name=libc.so
run "$as" --version
run "$ld" --version

for executable in \
    "$gcc" \
    "$gcclib/cc1" \
    "$gcclib/cc1plus" \
    "$gcclib/collect2" \
    "$as" \
    "$ld"
do
    inspect_host_elf "$executable"
done

run "$gcclib/cc1" --version
run "$gcclib/cc1plus" --version
run "$gcclib/collect2" --version

echo "===== preprocessor search paths (empty input; no compilation) ====="
printf '' | "$gcc" -E -v -x c - 2>&1
echo "exit=$?"

echo "===== compiler-visible target assets ====="
for asset in \
    "$toolroot/target/usr/lib/crt1.o" \
    "$toolroot/target/usr/lib/crti.o" \
    "$toolroot/target/usr/lib/crtn.o" \
    "$toolroot/target/usr/lib/libc.so" \
    "$toolroot/target/lib/libc.so.6" \
    "$toolroot/target/lib/ld-linux.so.3" \
    "$gcclib/libgcc.a" \
    "$toolroot/armv5teb-montavista-linuxeabi/lib/libgcc_s.so.1" \
    "$toolroot/target/usr/lib/libmoxalib.so.1.0.0" \
    "$toolroot/target/usr/include/moxadevice.h"
do
    ls -ld "$asset"
done

echo "===== authoritative Moxa definitions ====="
grep -E '^#define (MOXA_(SET|GET)_(OP_MODE|SPECIAL_BAUD_RATE)|RS232_MODE|RS485_2WIRE_MODE|RS422_MODE|RS485_4WIRE_MODE)' \
    "$toolroot/target/usr/include/moxadevice.h"
