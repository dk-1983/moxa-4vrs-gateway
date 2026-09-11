"""Local public-fixture qualification; run inside offline Ubuntu 24.04."""
from pathlib import Path
import hashlib
import json
import platform
import subprocess
import sys

root, binaries, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
evidence = {'scope': 'local public fixtures only; no physical CF, target or production seed',
            'os_release': Path('/etc/os-release').read_text(),
            'python': platform.python_version(), 'machine': platform.machine(),
            'kernel': platform.release(), 'tests': {}, 'sha256': {}}
for mode, worker in [('host', 'worker-fixture'), ('ubsan', 'worker-fixture-ubsan')]:
    command = [sys.executable, str(root/'tests/web/test_cf_bootstrap.py'),
               str(binaries/worker), str(binaries/'4vrs-cf-rng-worker'),
               str(output/('fixtures-'+mode)), str(binaries/('codec-'+mode))]
    result = subprocess.run(command, capture_output=True, timeout=120)
    (output/(mode+'.log')).write_bytes(result.stdout+result.stderr)
    evidence['tests'][mode] = {'exit_code': result.returncode,
                              'summary': result.stdout.decode().splitlines()[-1]}
    print(mode, evidence['tests'][mode], flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
for name in ['4vrs-cf-rng-worker', 'worker-fixture', 'worker-fixture-ubsan', 'codec-host', 'codec-ubsan']:
    evidence['sha256'][name] = hashlib.sha256((binaries/name).read_bytes()).hexdigest()
for name in ['src/host/cf_rng_writer.c', 'src/web/rng_nv.c', 'src/web/rng_config.h',
             'tools/cf-bootstrap.py', 'tools/build-cf-bootstrap.py',
             'tests/web/cf_public_random.c', 'tests/web/test_cf_bootstrap.py', 'tests/web/test_cf_target_codec.c']:
    evidence['sha256'][name] = hashlib.sha256((root/name).read_bytes()).hexdigest()
result = subprocess.run(['ldd', str(binaries/'4vrs-cf-rng-worker')], capture_output=True, check=True)
evidence['worker_dependencies'] = result.stdout.decode()
(output/'evidence.json').write_text(json.dumps(evidence, indent=2)+'\n')
