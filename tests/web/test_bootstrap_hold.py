"""Local shell fixture. Replaces both target paths before starting any process."""
from pathlib import Path
import os
import subprocess
import tempfile
import time

root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='console-hold-public-') as tmp:
 d=Path(tmp);marker=d/'hold';started=d/'started';getty=d/'fake-getty';script=d/'hold.sh'
 getty.write_text('#!/bin/sh\nprintf "%s\\n" "$*" > "'+str(started)+'"\n');getty.chmod(0o700)
 text=(root/'tools/hold-ttyS1.sh').read_text().replace('/var/run/4vrs-console-ttyS1.hold',str(marker)).replace('/sbin/getty',str(getty))
 assert '/var/run/4vrs-console-' not in text and '/sbin/getty' not in text
 script.write_text(text);marker.touch()
 p=subprocess.Popen(['/bin/sh',script]);time.sleep(1.2)
 assert p.poll() is None and not started.exists();print('PASS marker prevents getty')
 p.terminate();p.wait(timeout=3)
 p=subprocess.Popen(['/bin/sh',script]);time.sleep(1.2)
 assert p.poll() is None and not started.exists();print('PASS simulated supervisor restart still holds')
 marker.unlink();p.wait(timeout=3)
 assert p.returncode==0 and started.read_text()=='115200 ttyS1 -L\n';print('PASS explicit release execs original getty arguments')
 print('TOTAL 3 PASS; public temporary shell fixture, no target getty')
