#!/usr/bin/env python3
"""Separate qualification ELF; never overwrites the production build entry."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root, output = map(Path, sys.argv[1:])
sources = re.findall(r'"\$root/(src/[^"\s]+\.c)"',
                     (root / 'tools/build-gateway-application.sh').read_text())
assert sources[0] == 'src/app/main.c' and len(set(sources)) == len(sources)
command = [os.environ.get('CC', '/usr/local/xscale_be/bin/xscale_be-gcc'),
           *shlex.split(os.environ.get('CFLAGS', '-std=c99 -Os -Wall -Wextra -Werror -mcpu=xscale -mbig-endian -msoft-float')),
           '-I' + str(root / 'src'), str(root / 'tests/installer/qualification_main.c'),
           *[str(p) for p in sorted((root / 'src/installer').glob('*.c')) if p.name != 'main.c'],
           *[str(root / s) for s in sources[1:]], '-lrt', '-o', str(output)]
print(shlex.join(command), flush=True)
subprocess.run(command, check=True)
