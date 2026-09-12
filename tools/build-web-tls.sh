#!/bin/sh
set -eu
root=$1
tls=$2
out=$3
mode=${4:-host}
mkdir -p "$out/obj"
cc=${CC:-cc}
ar=ar
flags='-std=c99 -Os -Wall -Wextra'
if [ "$mode" = target ]; then
 cc=/usr/local/xscale_be/bin/xscale_be-gcc
 ar=/usr/local/xscale_be/bin/xscale_be-ar
 flags="$flags -mcpu=xscale -mbig-endian -msoft-float"
fi
if [ "$mode" = linux24 ]; then
 cc=/usr/local/mxscaleb/bin/mxscaleb-gcc
 ar=/usr/local/mxscaleb/bin/mxscaleb-ar
 flags='-std=c99 -Os -Wall -W -mcpu=xscale -mbig-endian -msoft-float -DFOURVRS_LINUX24'
fi
if [ "$mode" = ubsan ]; then flags="$flags -fsanitize=undefined -fno-sanitize-recover=all -g"; fi
for source in "$tls"/library/*.c; do
 name=$(basename "$source" .c)
 "$cc" $flags -DMBEDTLS_CONFIG_FILE='"web/mbedtls_config.h"' -I"$root/src" -I"$tls/include" -I"$tls/library" -c "$source" -o "$out/obj/$name.o"
done
"$ar" rcs "$out/libtls.a" "$out"/obj/*.o
"$cc" $flags -D_POSIX_C_SOURCE=200112L -DMBEDTLS_CONFIG_FILE='"web/mbedtls_config.h"' -I"$root/src" -I"$tls/include" "$root/src/web/tls_probe.c" "$out/libtls.a" -lrt -o "$out/tls-probe"
