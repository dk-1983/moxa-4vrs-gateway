"""Disposable public fixtures only. No physical CF or trusted-time simulation claim."""
from pathlib import Path
import os, signal, socket, struct, subprocess, sys, tempfile, hashlib

binary=Path(sys.argv[1]).resolve()
clocklib=binary.parent/'rng-clock-fixture.so'
subprocess.run(['cc','-shared','-fPIC',str(Path(__file__).with_name('rng_clock_fixture.c')),'-ldl','-o',clocklib],check=True)
def check(name,ok):
    assert ok,name
    print('PASS',name,flush=True)
def helper(d,op='status',data=None,env=None):
    args=[str(binary),op,str(d)]
    if op in ['production-enable','service-resume','service-recover']:args+=['--isolated-24h-confirmed']
    return subprocess.run(args,input=data,capture_output=True,env=env,timeout=10)
def service(d,op='production-enable',env=None):return helper(d,op,bytes(range(32)),env)
def launch(d,env=None):
    a,x=socket.socketpair();b,y=socket.socketpair()
    pid=os.fork()
    if not pid:
        xx=os.dup(x.fileno());yy=os.dup(y.fileno());os.dup2(xx,3);os.dup2(yy,4);os.closerange(5,1024)
        os.execve(str(binary),[str(binary),'serve',str(d)],env or os.environ)
    x.close();y.close();a.settimeout(5);b.settimeout(5);return pid,a,b
def request(a,seq=1):
    try:
        a.sendall(b'RNG1'+struct.pack('!III',seq,32,0));out=b''
        while len(out)<48:
            v=a.recv(48-len(out))
            if not v:break
            out+=v
        return out
    except (BrokenPipeError,ConnectionResetError):return b''
def end(pid,a,b,clean=True):
    if clean:os.kill(pid,signal.SIGTERM)
    _,status=os.waitpid(pid,0);a.close();b.close();return os.waitstatus_to_exitcode(status)
def snapshot(d):return {p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in d.iterdir() if p.is_file()}
def gen(d):return struct.unpack('!I',(d/'state').read_bytes()[8:12])[0]
def used(d):return struct.unpack('!I',(d/'state').read_bytes()[12:16])[0]
with tempfile.TemporaryDirectory(prefix='production-public-fixture-',dir=binary.parent) as tmp:
    root=Path(tmp)
    def case(name):
        d=root/name;d.mkdir(mode=0o700);return d
    d=case('budget');check('explicit bootstrap',service(d).returncode==0 and used(d)==1)
    check('production separate from trial',not (d/'trial-policy').exists() and (d/'production-policy').read_bytes()==b'production-v1\n')
    previous=None
    for expected in range(2,9):
        if expected>2:assert subprocess.run([binary,'service-arm',d,'--preserve-quota'],capture_output=True).returncode==0
        pid,a,b=launch(d);out=request(a)
        check('committed before output generation '+str(expected),len(out)==48 and gen(d)==expected and used(d)==expected and (d/'attempt').exists())
        check('no repeated stream on restart '+str(expected),out[16:]!=previous);previous=out[16:]
        check('clean stop '+str(expected),end(pid,a,b)==0 and not (d/'attempt').exists())
        check('startup permit never automatically restored '+str(expected),not (d/'start-permit').exists())
    before=snapshot(d)
    for clock in ['0','2147483647','4294967295']:
        env=dict(os.environ,TZ='Pacific/Kiritimati',NV_TEST_CLOCK=clock,LD_PRELOAD=str(clocklib))
        check('actual realtime substitution '+clock,subprocess.check_output(['date','+%s'],env=env).strip()==clock.encode())
        check('readonly quota status clock '+clock,helper(d,env=env).returncode==7 and snapshot(d)==before)
        pid,a,b=launch(d,env);check('ninth refused clock '+clock,not request(a) and end(pid,a,b,False)==7 and snapshot(d)==before)
    check('service assertion mandatory',subprocess.run([binary,'service-resume',d],input=bytes(32),capture_output=True).returncode==2 and snapshot(d)==before)
    check('service renewal consumes first slot',service(d,'service-resume').returncode==0 and gen(d)==9 and used(d)==1)
    check('enable cannot reset production',service(d).returncode!=0 and gen(d)==9 and used(d)==1)
    d=case('rotation');assert service(d).returncode==0
    env=dict(os.environ,NV_FAST_ROTATION='1');pid,a,b=launch(d,env)
    for seq in range(1,7*1024+1):
        out=request(a,seq)
        assert len(out)==48,seq
    check('eighth generation serves entire allocation',used(d)==8)
    check('ninth rotation refused',not request(a,7*1024+1) and end(pid,a,b,False)==7 and used(d)==8)
    stages=['permit-consumed','intent-created','intent-fsync','intent-directory-fsync','read','created','before-write','written','before-file-fsync','file-fsync','before-rename','renamed','before-directory-fsync','directory-fsync']
    for kind in ['NV_FAIL','NV_CRASH']:
        for stage in stages:
            d=case(kind+'-'+stage);assert service(d).returncode==0
            pid,a,b=launch(d,dict(os.environ,**{kind:stage}))
            check(kind+' no output '+stage,not request(a) and end(pid,a,b,False)!=0)
            before=snapshot(d)
            check(kind+' status read-only '+stage,helper(d).returncode==8 and snapshot(d)==before)
            pid,a,b=launch(d)
            check(kind+' no automatic retry '+stage,not request(a) and end(pid,a,b,False)==8 and snapshot(d)==before)
            check(kind+' explicit fresh service '+stage,service(d,'service-recover').returncode==0 and used(d)==1)
    d=case('migration');assert helper(d,'provision',bytes(range(32))).returncode==0
    (d/'trial-policy').write_bytes(b'trial-v1\n');(d/'trial-policy').chmod(0o600)
    pid,a,b=launch(d);oldout=request(a);assert len(oldout)==48;end(pid,a,b)
    oldgen=gen(d);oldstate=(d/'state').read_bytes()
    check('explicit migration count and seed evolution',service(d).returncode==0 and gen(d)==oldgen+1 and used(d)==1 and (d/'state').read_bytes()[96:128]!=oldstate[96:128])
    check('legacy policy revoked',not (d/'trial-policy').exists() and len((d/'state').read_bytes())==192)
    oldbinary=Path('/workspace/build/web-close-20260910/host/4vrs-rng')
    if oldbinary.exists():
        before=snapshot(d)
        check('actual old executable rejects downgrade',subprocess.run([oldbinary,'status',d],capture_output=True).returncode==4 and snapshot(d)==before)
    # A corrupt digest never falls back to trial or automatically reprovisions.
    value=bytearray((d/'state').read_bytes());value[100]^=1;(d/'state').write_bytes(value)
    before=snapshot(d)
    check('corruption distinguished',helper(d).returncode==4 and snapshot(d)==before)
    check('resume refuses corruption',service(d,'service-resume').returncode!=0 and snapshot(d)==before)
    check('explicit recovery fresh lineage',service(d,'service-recover').returncode==0 and used(d)==1)
    before=snapshot(d)
    check('no blind legacy bootstrap',helper(d,'provision',bytes(32)).returncode!=0 and snapshot(d)==before)
    check('secret payload absent from diagnostics',bytes(range(32)) not in helper(d).stdout)
    (d/'state').unlink();before=snapshot(d)
    check('lost production state requires service, not bootstrap',helper(d).returncode==8 and helper(d,'provision',bytes(32)).returncode!=0 and snapshot(d)==before)
    check('lost state recovery needs explicit fresh service',service(d,'service-recover').returncode==0)
    for stage in ['before-intent-clear','intent-cleared']:
        d=case(stage);assert service(d).returncode==0
        pid,a,b=launch(d,dict(os.environ,NV_FAIL=stage));assert len(request(a))==48
        check('cleanup failure reported '+stage,end(pid,a,b)!=0)
        before=snapshot(d);pid,a,b=launch(d)
        check('cleanup failure cannot retry without permit '+stage,not request(a) and end(pid,a,b,False)==8 and snapshot(d)==before)
    for stage in ['intent-created','written','file-fsync','renamed','directory-fsync','before-intent-clear','intent-cleared']:
        d=case('migration-fail-'+stage);assert helper(d,'provision',bytes(range(32))).returncode==0
        (d/'trial-policy').write_bytes(b'trial-v1\n');(d/'trial-policy').chmod(0o600)
        check('migration failure reported '+stage,service(d,env=dict(os.environ,NV_FAIL=stage)).returncode==8)
        check('migration never restores trial '+stage,not (d/'trial-policy').exists())
        before=snapshot(d);pid,a,b=launch(d)
        check('failed migration no automatic write '+stage,not request(a) and end(pid,a,b,False)==8 and snapshot(d)==before)
        check('failed migration explicit recovery '+stage,service(d,'service-recover').returncode==0 and used(d)==1)
print('Production-v1 local checks complete; service quarantine is an operator attestation, not a local clock test.')
