#!/usr/bin/python3
"""Public-only receiver fixture: assertions are observable via exit status."""
import os
from pathlib import Path
import resource
import stat
import sys
import time

mode, folder = sys.argv[1:]
d = Path(folder)
if not stat.S_ISFIFO(os.fstat(0).st_mode) or os.isatty(0):
    sys.exit(9)
if resource.getrlimit(resource.RLIMIT_CORE) != (0, 0):
    sys.exit(9)
if Path('/proc/self/fd/70000').exists():
    sys.exit(9)
for name in os.listdir('/proc/self/fd'):
    if int(name) > 2 and Path('/proc/self/fd', name).exists():
        sys.exit(9)
if mode == 'status':
    sys.exit(5 if (d / 'state').exists() else 3)
payload = sys.stdin.buffer.read(33)
if payload != bytes(255 if i == 5 else i for i in range(32)):
    sys.exit(9)
if (d / 'fail').exists():
    sys.exit(7)
if (d / 'hang').exists():
    time.sleep(60)
with (d / 'state').open('x') as f:
    f.write('PUBLIC FIXTURE COMMITTED\n')
# No payload, checksum, or helper output is forwarded by the receiver.
print('fixture output must be suppressed')
