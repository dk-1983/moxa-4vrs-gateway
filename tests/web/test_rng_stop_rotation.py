"""Stop during a real broker rotation; public disposable state only."""
from pathlib import Path
import subprocess,sys,os,socket,signal,struct,tempfile,time,json
build=Path(sys.argv[1]);probe=Path(sys.argv[2]);report=Path(sys.argv[3])
with tempfile.TemporaryDirectory(prefix='rotation-stop-public-',dir=build) as tmp:
    d=Path(tmp);d.chmod(0o700)
    assert subprocess.run([build/'4vrs-rng','production-enable',d,'--fresh-entropy-confirmed'],input=bytes(range(32)),capture_output=True).returncode==0
    a,x=socket.socketpair();b,y=socket.socketpair();logpath=d/'probe.log';log=logpath.open('w');pid=os.fork()
    if not pid:
        xx=os.dup(x.fileno());yy=os.dup(y.fileno());os.dup2(log.fileno(),2);os.dup2(xx,3);os.dup2(yy,4);os.closerange(5,1024)
        os.execve(build/'4vrs-rng',[str(build/'4vrs-rng'),'serve',str(d)],dict(os.environ,LD_PRELOAD=str(probe),NV_FAST_ROTATION='1',RNG_SLOW_FILE='pending',RNG_SLOW_HIT='2',RNG_SLOW_MS='4500'))
    x.close();y.close();a.settimeout(10)
    for i in range(1,1025):
        a.sendall(b'RNG1'+struct.pack('!III',i,32,0));reply=b''
        while len(reply)<48:reply+=a.recv(48-len(reply))
    a.sendall(b'RNG1'+struct.pack('!III',1025,32,0))
    end=time.monotonic()+10
    while 'probe slow-begin' not in logpath.read_text():
        assert time.monotonic()<end;time.sleep(.01)
    os.kill(pid,signal.SIGTERM);assert not a.recv(48)
    assert os.waitstatus_to_exitcode(os.waitpid(pid,0)[1])==0
    a.close();b.close();log.close()
    assert logpath.read_text().count('rng-allocation')==1024
    assert (d/'state').read_bytes()==(d/'witness').read_bytes()
    assert int.from_bytes((d/'state').read_bytes()[8:12],'big')==3
    assert subprocess.run([build/'4vrs-rng','status',d],capture_output=True).returncode==0
report.write_text(json.dumps({'result':'PASS','committed_generation':3,'requests_before_rotation':1024,'output_after_stop':False,'post_stop_status':'ready'},indent=2)+'\n')
print('PASS SIGTERM during rotation: commit completes, no allocation or output after stop')
