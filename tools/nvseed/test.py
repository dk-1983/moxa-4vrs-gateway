"""Deterministic lifecycle contracts, not randomness statistics or power-loss proof.
All public fixtures are explicitly non-secret and never used by the target plan.
Temporary state stays in the disposable build volume; never export it.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

binary = Path(sys.argv[1]).resolve()
checks = []
seed = bytes(range(32))  # PUBLIC HOST FIXTURE ONLY

def run(directory, action='consume', identity='fixture-device-A', data=None, fault=None):
    env = {k: v for k, v in os.environ.items() if k not in ('NV_FAIL', 'NV_CRASH', 'NV_BAD_SUPER')}
    if fault: env[fault[0]] = fault[1]
    return subprocess.run([binary, action, directory, identity], input=data,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=35)

def ok(r):
    assert r.returncode == 0, (r.returncode, r.stdout.decode(), r.stderr.decode())

def denied(r):
    assert r.returncode != 0 and b'runtime-output' not in r.stdout

with tempfile.TemporaryDirectory(prefix='nv-fixtures-', dir=binary.parent) as tmp:
    base = Path(tmp)
    def fresh(name):
        d = base / name; d.mkdir(mode=0o700)
        ok(run(d, 'provision', data=seed)); return d
    d = fresh('ordering')
    r = run(d); ok(r)
    events = [line.removeprefix('stage=') for line in r.stdout.decode().splitlines() if line.startswith('stage=')]
    assert events.index('file-fsync') < events.index('renamed') < events.index('directory-fsync') < events.index('runtime-output')
    assert int.from_bytes((d/'state').read_bytes()[8:12], 'big') == 2
    # Independent hashlib oracle for pinned entropy.c's initial NV extraction:
    # source 0, length 32; hash accumulator then hash result. No custom crypto.
    next_seed = hashlib.sha256(hashlib.sha256(b'\x00\x20'+seed).digest()).digest()
    assert (d/'state').read_bytes()[96:128] == next_seed
    ok(run(d)); assert int.from_bytes((d/'state').read_bytes()[8:12], 'big') == 3
    assert (d/'state').read_bytes()[96:128] == hashlib.sha256(hashlib.sha256(b'\x00\x20'+next_seed).digest()).digest()
    checks += ['durable-before-output', 'consume-restart-monotonic-state', 'pinned-nv-transform-independent-hashlib-oracle']
    for kind in ['NV_FAIL', 'NV_CRASH']:
        for boundary in ['locked', 'read', 'created', 'before-write', 'written', 'before-file-fsync',
                         'file-fsync', 'before-rename', 'renamed', 'before-directory-fsync', 'directory-fsync']:
            d = fresh(kind+'-'+boundary); old = (d/'state').read_bytes()
            r = run(d, fault=(kind, boundary)); denied(r)
            if kind == 'NV_CRASH': assert r.returncode == 77
            visible = (d/'state').read_bytes()
            expected = 2 if boundary in ['renamed','before-directory-fsync','directory-fsync'] else 1
            assert int.from_bytes(visible[8:12], 'big') == expected
            ok(run(d)); assert int.from_bytes((d/'state').read_bytes()[8:12], 'big') == expected+1
            assert not (d/'pending').exists()
            checks.append(kind+':'+boundary)
    # Crash after issuance must not reuse consumed generation on restart.
    d = fresh('issued-crash'); r = run(d, fault=('NV_CRASH', 'runtime-output'))
    assert r.returncode == 77 and b'runtime-output' in r.stdout
    ok(run(d)); assert int.from_bytes((d/'state').read_bytes()[8:12], 'big') == 3
    checks.append('post-output-crash-consumes-new-generation')
    for boundary in ['created','written','file-fsync','renamed','directory-fsync']:
        d = base/('provision-crash-'+boundary); d.mkdir(mode=0o700)
        denied(run(d, 'provision', data=seed, fault=('NV_CRASH', boundary)))
        if boundary in ['renamed','directory-fsync']:
            ok(run(d))
        else:
            denied(run(d)); assert not (d/'pending').exists()
            ok(run(d, 'provision', data=seed)); ok(run(d))
        checks.append('provision-crash:'+boundary)
    d = fresh('concurrent')
    jobs = [subprocess.Popen([binary, 'consume', d, 'fixture-device-A'], stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(8)]
    generations = []
    for p in jobs:
        out, err = p.communicate(timeout=35); assert p.returncode == 0, err.decode()
        generations.append(int(out.decode().split('generation=')[1].strip()))
    assert sorted(generations) == list(range(2, 10))
    assert int.from_bytes((d/'state').read_bytes()[8:12], 'big') == 9
    checks.append('eight-process-exclusive-generation-allocation')
    d = fresh('fork'); ok(run(d, 'fork-test')); checks.append('fork-child-poison-even-with-equal-pid-parent-remains-usable')
    d = fresh('limit'); ok(run(d, 'limit-test')); checks.append('1024-request-limit-fails-closed')
    d = fresh('missing'); (d/'state').unlink(); denied(run(d)); checks.append('missing-seed-no-fallback')
    d = fresh('corrupt'); b = bytearray((d/'state').read_bytes()); b[99] ^= 1; (d/'state').write_bytes(b)
    denied(run(d)); checks.append('corrupt-record')
    d = fresh('truncated'); (d/'state').write_bytes(b'broken'); denied(run(d)); checks.append('truncated-record')
    d = fresh('device-change'); denied(run(d, identity='fixture-device-B')); checks.append('device-binding-mismatch')
    d = fresh('cf-change'); b = bytearray((d/'state').read_bytes()); b[48] ^= 1
    b[128:] = hashlib.sha256(b[:128]).digest(); (d/'state').write_bytes(b)
    denied(run(d)); checks.append('filesystem-binding-mismatch-with-valid-checksum')
    for value in ['magic', 'zero-uuid']:
        denied(run(d, fault=('NV_BAD_SUPER', value))); checks.append('superblock:'+value)
    for item in ['state', 'owner.lock', 'pending']:
        d = fresh('symlink-'+item); original = d/item
        if original.exists(): original.rename(d/'saved')
        else: (d/'saved').write_bytes(b''); (d/'saved').chmod(0o600)
        original.symlink_to(d/'saved'); denied(run(d)); checks.append('no-symlink:'+item)
    d = fresh('dir-symlink'); link = base/'link'; link.symlink_to(d, target_is_directory=True)
    denied(run(link)); checks.append('no-directory-symlink')
    for item in ['state', 'owner.lock']:
        d = fresh('mode-'+item); (d/item).chmod(0o644); denied(run(d)); checks.append('private-mode:'+item)
    d = fresh('dir-mode'); d.chmod(0o755); denied(run(d)); checks.append('private-directory-mode')
    d = fresh('hardlink'); os.link(d/'state', d/'duplicate'); denied(run(d)); checks.append('no-hardlinked-state')
    if os.geteuid() == 0:
        d = fresh('wrong-owner'); os.chown(d/'state', 12345, 12345); denied(run(d)); checks.append('state-owner-mismatch')
    d = fresh('exhausted'); b = bytearray((d/'state').read_bytes()); b[8:12] = b'\xff'*4
    b[128:] = hashlib.sha256(b[:128]).digest(); (d/'state').write_bytes(b)
    denied(run(d)); assert not (d/'pending').exists(); checks.append('generation-exhaustion-no-wrap')
    d = fresh('reprovision'); before = (d/'state').read_bytes(); denied(run(d, 'provision', data=seed))
    assert (d/'state').read_bytes() == before; checks.append('no-provision-over-existing-state')
    for length in [0, 31, 33]:
        d = base/('seed-length-'+str(length)); d.mkdir(mode=0o700)
        denied(run(d, 'provision', data=bytes(length))); assert not (d/'state').exists()
        checks.append('exact-provision-input:'+str(length))
    # Layout contract only: software rollback tree never contains NV state.
    releases = base/'releases'; releases.mkdir(); (releases/'version').write_text('new')
    d = fresh('nv-outside-releases'); ok(run(d)); before = (d/'state').read_bytes()
    shutil.rmtree(releases); releases.mkdir(); (releases/'version').write_text('old')
    assert (d/'state').read_bytes() == before; ok(run(d)); checks.append('installer-rollback-layout-boundary')
    # Documented NON-guarantee: a bypassing raw image restore is accepted.
    d = fresh('raw-restore'); old = (d/'state').read_bytes(); ok(run(d)); next_state = (d/'state').read_bytes()
    (d/'state').write_bytes(old); ok(run(d)); assert (d/'state').read_bytes() == next_state
    checks.append('raw-rollback-is-not-detected-explicit-limitation')
print(json.dumps({'passed':len(checks), 'contracts':checks, 'physical_power_loss_test':False}, indent=2))
