#!/bin/sh
set -eu
root=${1:?root};out=${2:?output};mkdir -p "$out"
export CC=cc CFLAGS="${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror}"
sh "$root/tools/build-network-crash-qualification.sh" "$root" "$out/qualification"
for scenario in apply-peer keep-before keep-after writer-timeout; do
 trial="/tmp/4vrs-qualification-$$-$scenario"
 rc=0
 "$out/qualification" "$trial" host "$scenario" normal >"$out/$scenario.log" 2>&1 || rc=$?
 if test -f "$trial/trace"; then cat "$trial/trace" >>"$out/$scenario.log"; fi
 md5sum "$trial/confirmed" "$trial/good" "$trial/interfaces" "$trial/resolver" >>"$out/$scenario.log"
 cat "$out/$scenario.log"
 test "$rc" = 0
done
for negative in wrong-identity no-effect no-boundary; do
 if "$out/qualification" "/tmp/4vrs-qualification-$$-$negative" host keep-before "$negative" >"$out/$negative.log" 2>&1; then
  echo "FAIL negative accepted: $negative";exit 1
 fi
 if grep 'result=PASS' "$out/$negative.log"; then exit 1;fi
 grep 'result=FAIL' "$out/$negative.log"
done
for invalid in /etc/4vrs-network /tmp/../etc/4vrs-network /tmp/4vrs-qualification-x/nested; do
 if "$out/qualification" "$invalid" host keep-before normal; then exit 1;fi
done
mkdir -p /var/hda/4vrs/tests
wrapper_store="/var/hda/4vrs/tests/qualification-wrapper-$$"
sum=$(md5sum "$out/qualification");sum=${sum%% *}
sh "$root/tools/run-network-crash-qualification.sh" keep-before "$wrapper_store" host "$out/qualification" "$sum" run
cp "$wrapper_store.log" "$out/wrapper.log"
grep 'HARNESS_EXIT=0' "$out/wrapper.log"
if sh "$root/tools/run-network-crash-qualification.sh" keep-before "$wrapper_store" host "$out/qualification" "$sum" run; then exit 1;fi
if sh "$root/tools/run-network-crash-qualification.sh" keep-before /tmp/4vrs-qualification-wronghash host "$out/qualification" wrong run; then exit 1;fi
ln -s /etc /tmp/4vrs-qualification-link
if "$out/qualification" /tmp/4vrs-qualification-link host keep-before normal; then exit 1;fi
if sh "$root/tools/run-network-crash-qualification.sh" keep-before /tmp/4vrs-qualification-link/nested host "$out/qualification" "$sum" run; then exit 1;fi
if "$out/qualification" /tmp/4vrs-qualification-target unknown keep-before normal; then exit 1;fi
echo 'qualification positive=4 negative=3 path-refusals=3 wrapper/hash/reuse/symlink/target PASS'
