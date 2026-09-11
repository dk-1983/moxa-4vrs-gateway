"""Real lost acknowledgement and slow reader, with disposable public fixtures."""
from pathlib import Path
import json,os,socket,struct,subprocess,sys,tempfile,time,signal
binary=Path(sys.argv[1]).resolve();passed=[]
def check(name,ok):
    assert ok,name
    passed.append(name);print('PASS',name,flush=True)
with tempfile.TemporaryDirectory(prefix='rng-transport-',dir=binary.parent) as tmp:
    d=Path(tmp)
    p=subprocess.Popen([binary,'provision',d],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    p.stdout.close()  # ACK receiver really disappears before durable provision.
    p.stdin.write(bytes(range(32)));p.stdin.close();p.wait(timeout=5);errors=p.stderr.read();p.stderr.close()
    status=subprocess.run([binary,'status',d],capture_output=True,timeout=5)
    check('closed ACK pipe resolved by nonsecret status',status.returncode==5 and b'generation=1\n' in status.stdout)
    check('lost ACK does not log payload',bytes(range(32)) not in errors+status.stdout+status.stderr)
    before=(d/'state').read_bytes()
    again=subprocess.run([binary,'provision',d],input=bytes(range(32)),capture_output=True,timeout=5)
    check('lost ACK cannot cause blind reprovision',again.returncode!=0 and (d/'state').read_bytes()==before)
    (d/'trial-policy').write_bytes(b'trial-v1\n');(d/'trial-policy').chmod(0o600)
    a,x=socket.socketpair();b,y=socket.socketpair();x.setsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF,1024)
    pid=os.fork()
    if not pid:
        xx=os.dup(x.fileno());yy=os.dup(y.fileno());os.dup2(xx,3);os.dup2(yy,4);os.closerange(5,1024)
        os.execv(binary,[str(binary),'serve',str(d)])
    x.close();y.close()
    try:
        a.sendall(b''.join(b'RNG1'+struct.pack('!III',i,1024,0) for i in range(1,9)))
        # Eight legal headers, below saturation threshold; intentionally do not
        # drain replies. The response buffer must time out, not grow indefinitely.
        end=time.monotonic()+6;result=None
        while time.monotonic()<end:
            got,code=os.waitpid(pid,os.WNOHANG)
            if got:result=os.waitstatus_to_exitcode(code);pid=0;break
            time.sleep(.02)
        check('slow reply reader reaches bounded output deadline',result==21)
    finally:
        a.close();b.close()
        if pid:os.kill(pid,signal.SIGKILL);os.waitpid(pid,0)
result={'binary':str(binary),'passed':passed,'production_seed':False,'test_sha256':__import__('hashlib').sha256(Path(__file__).read_bytes()).hexdigest()}
if len(sys.argv)>2:Path(sys.argv[2]).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
