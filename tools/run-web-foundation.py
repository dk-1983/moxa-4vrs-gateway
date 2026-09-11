"""Build focused host/UBSan or target ABI executables from a source snapshot."""
import os
import re
import subprocess
import sys
from pathlib import Path
root, out = map(Path, sys.argv[1:3])
mode = sys.argv[3] if len(sys.argv)>3 else 'host'
out.mkdir(parents=True, exist_ok=True)
flags = ['-std=c99','-O2','-Wall','-Wextra','-Werror']
cc = 'cc'
if mode == 'ubsan': flags += ['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
if mode == 'target':
    cc='/usr/local/xscale_be/bin/xscale_be-gcc'
    flags += ['-mcpu=xscale','-mbig-endian','-msoft-float']
sources = re.findall(r'"\$root/([^"\n]+\.c)"', (root/'tools/run-host-application-tests.sh').read_text().split('"$out/test-gateway-application"\n')[0])
sources[0]='tests/web/test_web_control.c'
sources += ['src/web/web_control.c']
for name, files in [('web-control',sources),('key-repeat',['tests/panel/test_key_repeat.c','src/panel/key_repeat.c']),('keypad-event-probe',['tools/keypad-event-probe.c'])]:
    command=[cc]+flags+['-I'+str(root/'src'),'-I'+str(root/'tests'),'-o',str(out/name)]+[str(root/p) for p in files]+['-lrt']
    subprocess.run(command,check=True)
    if mode!='target' and name!='keypad-event-probe': subprocess.run([str(out/name)],check=True)
