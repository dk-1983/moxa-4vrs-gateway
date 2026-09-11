#!/bin/sh
# Shell builtins and exec only; target has no cmp/grep -x/readlink.
test "$#" -eq 0 || exit 2
case "$0" in
 */*) observer_dir=${0%/*} ;;
 *) observer_dir=. ;;
esac
exec "$observer_dir/4vrs-qualification-observer" event-before.txt
