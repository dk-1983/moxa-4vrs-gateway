"""Linux PTY tests; no physical USB/serial, no production entropy or state."""
import fcntl
import importlib.util
import os
from pathlib import Path
import pty
import select
import signal
import struct
import sys
import tempfile
import termios
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('client', ROOT / 'tools/rng-bootstrap.py')
client = importlib.util.module_from_spec(spec); spec.loader.exec_module(client)
binary = str(Path(sys.argv[1]).resolve())
real_helper = str(Path(sys.argv[2]).resolve())
fixture_source = ROOT / 'tests/web/bootstrap_helper_fixture.py'
checks = []

def check(name, value):
    assert value, name
    checks.append(name); print('PASS', name, flush=True)

class Session:
    def __init__(self, folder, mode='probe', helper=None, competitor=False, leader=True):
        helper = str(fixture) if helper is None else helper
        self.master, slave = pty.openpty(); self.name = os.ttyname(slave)
        self.saved = termios.tcgetattr(slave); self.peer = slave if competitor else None
        self.pid = os.fork()
        if not self.pid:
            os.close(self.master)
            if leader:
                os.setsid(); fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
            os.dup2(slave, 70000, inheritable=True)
            for fd in range(3): os.dup2(slave, fd)
            if slave > 2: os.close(slave)
            os.execv(binary, [binary, mode, self.name, helper, '--reserved-console', str(folder)])
        if not competitor: os.close(slave)
        self.transcript = bytearray()
        # Wait for raw mode; sending the greeting into an ordinary terminal
        # is not a valid test of the receiver handshake.
        end = time.monotonic() + 3
        while time.monotonic() < end:
            if not termios.tcgetattr(self.master)[3] & termios.ICANON: break
            p, status = os.waitpid(self.pid, os.WNOHANG)
            if p: self.result = os.waitstatus_to_exitcode(status); self.pid = 0; break
            time.sleep(.01)

    def read(self, n):
        if not select.select([self.master], [], [], .1)[0]: return b''
        try: block = os.read(self.master, n)
        except OSError: block = b''
        self.transcript.extend(block); return block

    def write(self, data):
        return os.write(self.master, data)

    def finish(self, expected=None, timeout=13):
        end = time.monotonic() + timeout
        while self.pid and time.monotonic() < end:
            p, status = os.waitpid(self.pid, os.WNOHANG)
            if p: self.result = os.waitstatus_to_exitcode(status); self.pid = 0; break
            time.sleep(.01)
        if self.pid:
            os.kill(self.pid, signal.SIGKILL); os.waitpid(self.pid, 0); raise AssertionError('receiver hung')
        check('terminal restored', termios.tcgetattr(self.master) == self.saved)
        reopened = os.open(self.name, os.O_RDWR | os.O_NOCTTY); os.close(reopened)
        check('terminal exclusive access released', True)
        while self.read(256): pass
        check('no payload echo or helper logs', client.PUBLIC not in self.transcript and b'fixture output' not in self.transcript)
        os.close(self.master)
        if self.peer is not None: os.close(self.peer)
        if expected is not None: check('receiver exit ' + str(expected), self.result == expected)

def greeting(s, mode='T'):
    s.write(b'RBH1' + mode.encode() + b'\0\0\0')
    return client.exact(s, 8, time.monotonic() + 4)

def frame(mode='T'):
    data = b'RBF1' + mode.encode() + b'\0\0\x20' + client.PUBLIC
    return data + struct.pack('>I', zlib.crc32(data)) + b'RBE1DONE'

with tempfile.TemporaryDirectory(prefix='bootstrap-public-') as temp:
    root = Path(temp)
    fixture = root/'helper-fixture'; fixture.write_bytes(fixture_source.read_bytes()); fixture.chmod(0o755)
    d = root / 'probe'; d.mkdir(mode=0o700)
    s = Session(d)
    check('receiver high fd closed', not Path('/proc',str(s.pid),'fd','70000').exists())
    check('launcher to actual PTY receiver public probe', client.exchange(s, 'probe') == 'probe-passed')
    s.finish(0); check('probe writes no state', not list(d.iterdir()))
    public_frame = frame('P')
    for label, data in [('fragmented', public_frame), ('extra', public_frame+b'X'), ('repeated', public_frame*2),
                        ('short', public_frame[:39]), ('no-finish', public_frame[:44]),
                        ('wrong-length', public_frame[:7]+b'\x21'+public_frame[8:]),
                        ('bad-checksum', public_frame[:8]+b'X'+public_frame[9:]),
                        ('wrong-finish', public_frame[:-1]+b'X')]:
        d = root / label; d.mkdir()
        s = Session(d, 'provision'); check(label+' ready', greeting(s, 'P')[:6] == b'RBS1P\x03')
        if label == 'fragmented':
            for byte in data: s.write(bytes([byte])); time.sleep(.002)
        else: s.write(data)
        ack = client.exact(s, 8, time.monotonic()+12)
        check(label+' result', ack == (b'RBA1\0\0\0\0' if label=='fragmented' else b'RBA1\x07\0\0\0'))
        s.finish(0 if label=='fragmented' else 7)
        check(label+' write boundary', (d/'state').exists() == (label == 'fragmented'))
    s = Session(d); greeting(s); os.kill(s.pid, signal.SIGTERM); s.finish(7)
    for sig in (signal.SIGINT, signal.SIGHUP):
        s = Session(d); greeting(s); os.kill(s.pid, sig); s.finish(7)
    s = Session(d, competitor=True); s.finish(7)
    s = Session(d, leader=False); s.finish(7)
    for label, helper in [('fixture', str(fixture)), ('real', real_helper)]:
        d = root / label; d.mkdir(mode=0o700)
        s = Session(d, 'provision', helper)
        check(label+' pipe provision', 'provision-complete' in client.exchange(s, 'provision', lambda v: v.__setitem__(slice(None), client.PUBLIC)))
        s.finish(0); before = (d/'state').read_bytes()
        s = Session(d, 'status', helper)
        check(label+' subsequent status', client.exchange(s, 'status') == 'policy-required'); s.finish(0)
        s = Session(d, 'provision', helper)
        try: client.exchange(s, 'provision', lambda v: (_ for _ in ()).throw(AssertionError('CSPRNG must not run')))
        except client.BootstrapError: pass
        else: raise AssertionError('existing state accepted')
        s.finish(7); check(label+' state preserved', (d/'state').read_bytes() == before)
    d = root/'failed'; d.mkdir(); (d/'fail').touch()
    s = Session(d, 'provision'); greeting(s, 'P'); s.write(frame('P'))
    check('helper failure', client.exact(s,8,time.monotonic()+4)[4] == 7); s.finish(7)
    check('failed helper no state', not (d/'state').exists())
    d = root/'hung'; d.mkdir(); (d/'hang').touch()
    s = Session(d, 'provision'); greeting(s,'P'); s.write(frame('P'))
    started = time.monotonic()
    check('helper deadline failure', client.exact(s,8,time.monotonic()+40)[4] == 7)
    check('helper deadline bounded', 34 <= time.monotonic()-started < 39)
    s.finish(7); check('hung helper no state', not (d/'state').exists())
    s = Session(d)
    started = time.monotonic(); s.finish(7, timeout=33)
    check('public greeting deadline bounded', 28 <= time.monotonic()-started < 33)
    d = root/'lost'; d.mkdir()
    s = Session(d, 'provision'); greeting(s, 'P'); s.write(frame('P'))
    # Discard an ACK already sent by the receiver: models desktop loss, not a
    # physical USB disconnect or power failure. Status resolves durable outcome.
    s.finish(0)
    s = Session(d, 'status'); check('lost ACK status first', client.exchange(s,'status') == 'policy-required'); s.finish(0)
print('TOTAL', len(checks), 'PASS; public fixtures only')
