#!/bin/sh
# Local target command only; this script never opens SSH or changes real networking.
# CASE NEW_STORE TARGET_LABEL BINARY EXPECTED_MD5 run
set -eu
test "$#" = 6 || { echo 'usage: CASE NEW_STORE TARGET_LABEL BINARY EXPECTED_MD5 run'; exit 2; }
scenario=$1; store=$2; target=$3; binary=$4; expected=$5
test "$6" = run || exit 2
case "$scenario" in apply-peer|keep-before|keep-after|writer-timeout) ;; *) exit 2;; esac
case "$target" in moxa1|moxa2|host) ;; *) exit 2;; esac
case "$store" in
 /var/hda/4vrs/tests/qualification-*) suffix=${store#/var/hda/4vrs/tests/qualification-}; parents='/var /var/hda /var/hda/4vrs /var/hda/4vrs/tests';;
 /tmp/4vrs-qualification-*) suffix=${store#/tmp/4vrs-qualification-}; parents=/tmp;;
 *) echo 'REFUSE live/unknown path'; exit 2;;
esac
case "$suffix" in ''|*[!a-z0-9-]*) exit 2;; esac
for parent in $parents; do test -d "$parent" && test ! -L "$parent" || exit 2; done
test ! -e "$store" && test ! -L "$store" || { echo 'REFUSE existing store'; exit 2; }
test ! -e "$store.log" && test ! -L "$store.log" || exit 2
actual=$(md5sum "$binary"); actual=${actual%% *}
test "$actual" = "$expected" || { echo 'REFUSE artifact hash'; exit 2; }
# Every output file is capped; instrumented barriers expire after ten seconds.
ulimit -f 128
set -C
{
 date -u; uname -a; hostname
 printf 'target_label=%s case=%s store=%s\n' "$target" "$scenario" "$store"
 md5sum "$binary"
 rc=0
 "$binary" "$store" "$target" "$scenario" normal || rc=$?
 test ! -f "$store/trace" || cat "$store/trace"
 for name in confirmed good candidate commit.guard interfaces resolver; do
  test ! -f "$store/$name" || md5sum "$store/$name"
 done
 printf 'HARNESS_EXIT=%s\n' "$rc"
 exit "$rc"
} >"$store.log" 2>&1
