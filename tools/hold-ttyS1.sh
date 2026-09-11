#!/bin/sh
# Staged service asset, never installed automatically. Keep init id/levels/action.
# Runtime marker must exist BEFORE installing the process-only inittab change.
# Clear it only after restoring inittab, confirming reload and clearing the link.
exec </dev/null >/dev/null 2>&1
while [ -e /var/run/4vrs-console-ttyS1.hold ]; do
    /bin/sleep 1
done
exec /sbin/getty 115200 ttyS1 -L
