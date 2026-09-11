#!/bin/sh
# Future authorized device operation, not called by build/tests/installer.
# Gateway transports are stopped during this diagnostic; schedule the outage.
set -eu
test "${1:-}" = --authorized-gateway-outage || {
  echo 'Requires explicit authorization for a Gateway outage and keypad access.' >&2
  exit 2
}
probe=/var/hda/4vrs/run/4vrs-keypad-event-probe
service=/etc/init.d/4vrs-gateway
test -x "$probe" && test -x "$service" || exit 1
# Require an initially running Gateway; never start a previously stopped one.
"$service" status || exit 1
restore(){ "$service" start; "$service" status; }
trap 'restore' EXIT
trap 'exit 130' INT TERM HUP
"$service" stop || exit 1
# Check every process, including unexpected consumers, before opening keypad.
for f in /proc/[0-9]*/fd/*; do
  if test "$f" -ef /dev/keypad; then
    echo 'Keypad still owned; diagnostic refused.' >&2
    exit 1
  fi
done
"$probe" --exclusive-keypad-authorized
