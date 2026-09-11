"""Retest persist-before-output with final IPC candidate, without device access."""
from pathlib import Path
import subprocess
root=Path('/workspace/source-host');base=Path('/workspace/build/web-ipc-v1-20260911')
probe=base/'slow-commit-fixture.so'
subprocess.run(['cc','-shared','-fPIC',root/'tests/web/rng_slow_commit_fixture.c','-ldl','-o',probe],check=True)
for mode in ['host','ubsan']:
    for kind in ['fixed','rotation']:
        args=[base/mode,probe,'fixed'] if kind=='fixed' else [base/mode,probe]
        script='test_rng_slow_commit.py' if kind=='fixed' else 'test_rng_stop_rotation.py'
        with (base/(kind+'-slow-'+mode+'.log')).open('wb') as log:
            subprocess.run(['python3',root/'tests/web'/script,*args,base/(kind+'-slow-'+mode+'.json')],stdout=log,stderr=subprocess.STDOUT,check=True)
        print('PASS',kind,mode,flush=True)
