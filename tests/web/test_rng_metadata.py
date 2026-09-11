"""Adversarial metadata checks on the production helper; disposable host state."""
from pathlib import Path
import hashlib,os,signal,socket,struct,subprocess,sys,tempfile,time
binary=Path(sys.argv[1]).resolve();passed=[]
def check(name,ok):
    assert ok,name
    passed.append(name);print('PASS',name,flush=True)
def helper(path,op='status',data=None,env=None):
    return subprocess.run([binary,op,path],input=data,capture_output=True,timeout=5,env=env)
def launch(path):
    a,x=socket.socketpair();b,y=socket.socketpair();pid=os.fork()
    if not pid:
        xx=os.dup(x.fileno());yy=os.dup(y.fileno());os.dup2(xx,3);os.dup2(yy,4);os.closerange(5,1024)
        os.execv(binary,[str(binary),'serve',str(path)])
    x.close();y.close();a.settimeout(5);b.settimeout(5);return pid,a,b
def reap(pid):
    end=time.monotonic()+5
    while time.monotonic()<end:
        p,s=os.waitpid(pid,os.WNOHANG)
        if p:return os.waitstatus_to_exitcode(s)
        time.sleep(.01)
    os.kill(pid,signal.SIGKILL);os.waitpid(pid,0);raise AssertionError('owner timeout')
with tempfile.TemporaryDirectory(prefix='rng-metadata-',dir=binary.parent) as temp:
    root=Path(temp);d=root/'nv';d.mkdir(mode=0o700)
    assert helper(d,'provision',bytes(range(32))).returncode==0
    original=(d/'state').read_bytes()
    check('nonsecret status generation',b'generation=1\n' in helper(d).stdout)
    for name,data in [('checksum',original[:96]+b'x'+original[97:]),('short',original[:-1]),('extra',original+b'x')]:
        (d/'state').write_bytes(data)
        check(name+' refused and preserved',helper(d).returncode==4 and (d/'state').read_bytes()==data)
    for name,offset in [('schema',7),('MAC binding',16),('CF UUID binding',48)]:
        data=bytearray(original);data[offset]^=1;data[128:]=hashlib.sha256(data[:128]).digest();(d/'state').write_bytes(data)
        check(name+' refused despite valid checksum',helper(d).returncode==4)
    (d/'state').write_bytes(original)
    for mode in (0o644,0o660):
        (d/'state').chmod(mode);check('state permissions '+oct(mode),helper(d).returncode==4)
    (d/'state').chmod(0o600)
    os.link(d/'state',d/'alias');check('state hardlink refused',helper(d).returncode==4);(d/'alias').unlink()
    (d/'state').rename(d/'held');(d/'state').symlink_to(d/'held');check('state symlink refused',helper(d).returncode==4);(d/'state').unlink();(d/'held').rename(d/'state')
    alias=root/'linked';alias.symlink_to(d);check('directory symlink refused',helper(alias).returncode==4);alias.unlink()
    d.chmod(0o755);check('directory permissions refused',helper(d).returncode==4);d.chmod(0o700)
    if os.geteuid()==0:
        os.chown(d/'state',1,1);check('wrong state owner refused',helper(d).returncode==4);os.chown(d/'state',0,0)
    for value in ('magic','uuid'):
        check('invalid CF superblock '+value,helper(d,env=dict(os.environ,NV_BAD_SUPER=value)).returncode==4)
    policy=d/'trial-policy';policy.write_bytes(b'trial-v1\n');policy.chmod(0o600)
    check('valid private policy',helper(d).returncode==0)
    policy.chmod(0o644);check('public policy refused',helper(d).returncode==5);policy.chmod(0o600)
    os.link(policy,d/'policy-link');check('hardlinked policy refused',helper(d).returncode==5);(d/'policy-link').unlink()
    # The actual production lock stays held until owner exit; a second exec
    # cannot publish or answer before it acquires ownership.
    p,a,b=launch(d);a.sendall(b'RNG1'+struct.pack('!III',1,32,0));assert len(a.recv(48))==48
    q,c,e=launch(d);c.sendall(b'RNG1'+struct.pack('!III',1,32,0));c.settimeout(.2)
    try:
        c.recv(48);raise AssertionError('second owner emitted while locked')
    except socket.timeout:check('exclusive owner prevents concurrent output',True)
    a.close();b.close();assert reap(p)==22;c.settimeout(5)
    reply=b''
    while len(reply)<48:reply+=c.recv(48-len(reply))
    check('successor commits new epoch after lock release',int.from_bytes(reply[12:16],'big')==3)
    c.close();e.close();assert reap(q)==22
    (d/'owner.lock').unlink();(d/'owner.lock').symlink_to(d/'state')
    p,a,b=launch(d)
    try:
        a.sendall(b'RNG1'+struct.pack('!III',1,32,0))
        try:reply=a.recv(48)
        except ConnectionResetError:reply=b''
        check('symlinked lock fails before output',not reply and reap(p)==4)
    finally:a.close();b.close()
print('RNG metadata:',len(passed),'checks; production seed=False')
