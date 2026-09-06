"""Compile against the same explicit source set as the full Gateway."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

root, out = map(Path, sys.argv[1:])
sources = re.findall(r'"\$root/(src/[^"\s]+\.c)"', (root / 'tools/build-gateway-application.sh').read_text())
assert sources and len(sources) == len(set(sources)) and sources[0] == 'src/app/main.c'
command = [os.environ.get('CC', 'cc'), *shlex.split(os.environ.get('CFLAGS', '-std=c99 -O2 -g -Wall -Wextra -Werror')),
           '-I' + str(root / 'src'), str(root / 'tests/installer/test_orchestrator.c'),
           *[str(path) for path in sorted((root / 'src/installer').glob('*.c')) if path.name != 'main.c'],
           *[str(root / name) for name in sources[1:]], '-lrt', '-Wl,--wrap=fsync', '-o', str(out / 'test-orchestrator')]
print(shlex.join(command), flush=True)
subprocess.run(command, check=True)
subprocess.run([str(out / 'test-orchestrator'), str(out)], check=True)
subprocess.run([sys.executable, str(root / 'tests/installer/test_boot_scripts.py'), str(root), str(out)], check=True)
