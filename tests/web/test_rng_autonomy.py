"""Disposable public fixtures. Process crash tests do not emulate CF power loss."""
from pathlib import Path
import os, signal, socket, struct, subprocess, sys, tempfile, hashlib, time
binary=Path(sys.argv[1]).resolve()
def check(name,ok):
    assert ok,name
    print('PASS',name,flush=True)
def helper(d,op='status',data=None,env=None):
    args=[str(binary),op,str(d)]
    if op in ['production-enable','service-resume','service-recover']:args+=['--fresh-entropy-confirmed']
    return subprocess.run(args,input=data,capture_output=True,env=env,timeout=10)
def service(d,op='production-enable',env=None):return helper(d,op,bytes(range(32)),env)
def launch(d,env=None):
    a,x=socket.socketpair();b,y=socket.socketpair();pid=os.fork()
    if not pid:
        xx=os.dup(x.fileno());yy=os.dup(y.fileno());os.dup2(xx,3);os.dup2(yy,4);os.closerange(5,1024)
        os.execve(str(binary),[str(binary),'serve',str(d)],env or os.environ)
    x.close();y.close();a.settimeout(5);return pid,a,b
def request(a,seq=1):
    try:
        a.sendall(b'RNG1'+struct.pack('!III',seq,32,0));out=b''
        while len(out)<48:
            v=a.recv(48-len(out))
            if not v:break
            out+=v
        return out
    except (BrokenPipeError,ConnectionResetError):return b''
def end(pid,a,b,sig=signal.SIGTERM):
    if sig:os.kill(pid,sig)
    _,status=os.waitpid(pid,0);a.close();b.close();return os.waitstatus_to_exitcode(status)
def snapshot(d):
    result={}
    for p in d.iterdir():
        if p.is_file():
            fd=os.open(p,os.O_RDONLY|os.O_NOATIME)
            try:data=os.read(fd,4096)
            finally:os.close(fd)
            st=p.stat();result[p.name]=(data,st.st_mtime_ns,st.st_atime_ns)
    return result
def gen(d):return struct.unpack('!I',(d/'state').read_bytes()[8:12])[0]
def put(d,name,data):
    (d/name).write_bytes(data);(d/name).chmod(0o600)
with tempfile.TemporaryDirectory(prefix='autonomy-public-fixture-',dir=binary.parent) as tmp:
    root=Path(tmp)
    def case(name):
        d=root/name;d.mkdir(mode=0o700);return d
    d=case('restarts');check('explicit fresh bootstrap',service(d).returncode==0 and gen(d)==1)
    check('distinct schema and no permit', (d/'state').read_bytes()[:8]==b'4VRSNV03' and not (d/'start-permit').exists())
    streams=set()
    for i in range(2,22):
        pid,a,b=launch(d);out=request(a)
        check('autonomous committed start '+str(i),len(out)==48 and gen(d)==i and (d/'state').read_bytes()==(d/'witness').read_bytes())
        check('unique restart output '+str(i),out[16:] not in streams);streams.add(out[16:])
        end(pid,a,b,signal.SIGKILL if i%2 else signal.SIGTERM)
        check('power-loss-after-commit classified ready '+str(i),helper(d).returncode==0)
    check('eight diagnostic only',b'threshold-reached=yes' in helper(d).stdout)
    clocklib=binary.parent/'rng-clock-fixture.so'
    subprocess.run(['cc','-shared','-fPIC',str(Path(__file__).with_name('rng_clock_fixture.c')),'-ldl','-o',clocklib],check=True)
    for clock in ['0','2147483647','4294967295']:
        env=dict(os.environ,NV_TEST_CLOCK=clock,LD_PRELOAD=str(clocklib));old=gen(d)
        check('actual realtime '+clock,subprocess.check_output(['date','+%s'],env=env).strip()==clock.encode())
        pid,a,b=launch(d,env);check('clock does not rewind '+clock,len(request(a))==48 and gen(d)==old+1);end(pid,a,b)
    # Read the fixture once before measuring no-atime reads.
    snapshot(d);before=snapshot(d)
    for _ in range(3):assert helper(d).returncode==0
    check('status performs no CF mutation',snapshot(d)==before)
    pid,a,b=launch(d);assert len(request(a))==48;before=snapshot(d)
    pid2,c,e=launch(d);check('concurrent owner fails without writes',not request(c) and end(pid2,c,e,None)==10 and snapshot(d)==before)
    check('first owner remains usable',len(request(a,2))==48);end(pid,a,b)
    # Existing witness remains equal until first staging write; after state rename
    # an observed exact successor resolves lost acknowledgement. All middle states stop.
    stages=['read','witness-created','before-witness-write','witness-written','before-witness-fsync','witness-fsync','before-witness-close','witness-closed','before-witness-rename','witness-renamed','before-witness-directory-fsync','witness-directory-fsync','created','before-write','written','before-file-fsync','file-fsync','before-close','closed','before-rename','renamed','before-directory-fsync','directory-fsync']
    resolved={'read','renamed','before-directory-fsync','directory-fsync'}
    for kind in ['NV_FAIL','NV_CRASH']:
        for stage in stages:
            d=case(kind+'-'+stage);assert service(d).returncode==0
            pid,a,b=launch(d,dict(os.environ,**{kind:stage}))
            check(kind+' output withheld '+stage,not request(a) and end(pid,a,b,None)!=0)
            snapshot(d);before=snapshot(d);status=helper(d)
            check(kind+' read-only classification '+stage,status.returncode==(0 if stage in resolved else 8) and snapshot(d)==before)
            if stage in resolved:
                old=gen(d);pid,a,b=launch(d);check(kind+' proven successor resumes '+stage,len(request(a))==48 and gen(d)==old+1);end(pid,a,b)
            else:
                for repeat in range(3):
                    pid,a,b=launch(d);check(kind+' ambiguous no retry '+stage+' '+str(repeat),not request(a) and end(pid,a,b,None)==8 and snapshot(d)==before)
                check(kind+' explicit fresh recovery '+stage,service(d,'service-recover').returncode==0)
    d=case('rotations');assert service(d).returncode==0
    pid,a,b=launch(d,dict(os.environ,NV_FAST_ROTATION='1'))
    for seq in range(1,10*1024+1):assert len(request(a,seq))==48
    check('beyond eighth and ninth rotation',gen(d)==11);end(pid,a,b)
    for fault in ['state','witness','pending','witness-next','attempt']:
        d=case('corrupt-'+fault);assert service(d).returncode==0
        if fault in ['state','witness']:
            value=bytearray((d/fault).read_bytes());value[100]^=1;put(d,fault,value)
        else:put(d,fault,b'partial')
        snapshot(d);before=snapshot(d);pid,a,b=launch(d)
        check('corruption no output or writes '+fault,not request(a) and end(pid,a,b,None) in [4,8] and snapshot(d)==before)
    # Real legacy helper creates each old format. No implicit trial promotion.
    oldbinary=Path('/workspace/build/rng-production-v1-20260911/host/4vrs-rng')
    assert oldbinary.exists()
    for schema in [1,2]:
        d=case('migration-'+str(schema))
        if schema==1:
            assert helper(d,'provision',bytes(range(32))).returncode==0;put(d,'trial-policy',b'trial-v1\n')
        else:
            assert subprocess.run([oldbinary,'production-enable',d,'--isolated-24h-confirmed'],input=bytes(range(32)),capture_output=True).returncode==0
        previous=(d/'state').read_bytes();old=gen(d)
        check('explicit migration '+str(schema),service(d).returncode==0 and gen(d)==old+1 and (d/'state').read_bytes()[96:128]!=previous[96:128])
        snapshot(d);before=snapshot(d)
        check('old binary downgrade refuses '+str(schema),subprocess.run([oldbinary,'status',d],capture_output=True).returncode==4 and snapshot(d)==before)
        pid,a,b=launch(d);check('migrated autonomous '+str(schema),len(request(a))==48);end(pid,a,b)
        snapshot(d);before=snapshot(d)
        check('enable cannot reset autonomous '+str(schema),service(d).returncode!=0 and snapshot(d)==before)
    d=case('migration-ambiguous');assert subprocess.run([oldbinary,'production-enable',d,'--isolated-24h-confirmed'],input=bytes(range(32)),capture_output=True).returncode==0
    put(d,'attempt',b'production-v1\n');snapshot(d);before=snapshot(d)
    check('legacy ambiguous migration refuses',service(d).returncode!=0 and snapshot(d)==before)
    check('legacy ambiguous explicit fresh recovery',service(d,'service-recover').returncode==0)
    for stage in ['service-policy']+stages[1:]:
        d=case('migration-failure-'+stage)
        assert helper(d,'provision',bytes(range(32))).returncode==0;put(d,'trial-policy',b'trial-v1\n')
        check('migration reports failure '+stage,service(d,env=dict(os.environ,NV_FAIL=stage)).returncode!=0)
        check('migration revokes trial '+stage,not (d/'trial-policy').exists())
        before=snapshot(d);status=helper(d)
        if stage in resolved:
            check('migration completed successor resolves '+stage,status.returncode==0)
        else:
            pid,a,b=launch(d);check('migration incomplete no retry '+stage,not request(a) and end(pid,a,b,None)!=0 and snapshot(d)==before)
        check('migration fresh recovery '+stage,service(d,'service-recover').returncode==0)
    check('secrets absent from diagnostics',bytes(range(32)) not in helper(d).stdout)
print('Autonomy checks complete. Physical durability and NAND wear not qualified.')
