#!/bin/sh
# Run only from the freshly staged qualification directory. Never a live install.
set -eu
test "$#" -eq 0 || exit 2
name=4vrs-installer-qualification-fixture-20260906-03
test -f "./$name" && test ! -L "./$name" || exit 2
actual=$(md5sum "./$name")
case "$actual" in
 '58a014e93d8ec52e003908286b30e93b  '*) ;;
 *) echo 'qualification artifact mismatch'; exit 2 ;;
esac
test ! -e /var/hda/4vrs/tests/installer-qualification-01 || exit 2
test ! -e qualification-result.txt || exit 2
(
 echo 'scope=isolated-filesystem-production-orchestration-fake-services'
 hostname
 date -u
 cat /proc/uptime
 md5sum "./$name"
 set +e
 "./$name" --isolated-first-and-cut
 result=$?
 echo "exit=$result"
 cat /proc/uptime
 exit "$result"
) >qualification-result.txt 2>&1
cat qualification-result.txt
