#!/usr/bin/env python3
"""Build fixed-root qualification fixture; compiler flags explicitly choose host."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
root, output = map(Path, sys.argv[1:])
sources = re.findall(r'"\$root/(src/[^"\s]+\.c)"', (root / 'tools/build-gateway-application.sh').read_text())
assert sources[0] == 'src/app/main.c' and len(set(sources)) == len(sources)
cmd = [os.environ.get('CC', '/usr/local/xscale_be/bin/xscale_be-gcc'),
       *shlex.split(os.environ.get('CFLAGS', '-std=c99 -Os -Wall -Wextra -Werror -mcpu=xscale -mbig-endian -msoft-float')),
       '-I' + str(root / 'src'), str(root / 'tests/installer/qualification_fixture.c'),
       *[str(p) for p in sorted((root / 'src/installer').glob('*.c')) if p.name != 'main.c'],
       *[str(root / s) for s in sources[1:]], '-Wl,--wrap=fsync', '-lrt', '-o', str(output)]
print(shlex.join(cmd), flush=True)
subprocess.run(cmd, check=True)
