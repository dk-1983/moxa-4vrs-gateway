#!/usr/bin/env python3
"""Maintainer-side build of the actual target installer entry point.

Python is not shipped or required on Moxa. The shared production networking
source set is taken from the Gateway build declaration to avoid a second list.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root, output = map(Path, sys.argv[1:])
sources = re.findall(r'"\$root/(src/[^"\s]+\.c)"',
                     (root / 'tools/build-gateway-application.sh').read_text())
if not sources or sources[0] != 'src/app/main.c' or len(sources) != len(set(sources)):
    raise SystemExit('unsupported Gateway build declaration')
installer = sorted((root / 'src/installer').glob('*.c'))
if root / 'src/installer/main.c' not in installer:
    raise SystemExit('actual installer entry point missing')
command = [os.environ.get('CROSS_CC', '/usr/local/xscale_be/bin/xscale_be-gcc'),
           *shlex.split(os.environ.get('CFLAGS', '-std=c99 -Os -Wall -Wextra -Werror -mcpu=xscale -mbig-endian -msoft-float')),
           '-I' + str(root / 'src'), *map(str, installer),
           *[str(root / name) for name in sources[1:]], '-lrt', '-o', str(output)]
print(shlex.join(command), flush=True)
subprocess.run(command, check=True)
print('actual installer:', output, output.stat().st_size, 'bytes')
