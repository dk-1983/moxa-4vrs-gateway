"""Real Gateway/Web/RNG, disposable state and mocked UART/network backends."""
from pathlib import Path
import subprocess,os,sys,signal,time,socket,json,tempfile
build=Path(sys.argv[1]).resolve()
def wait(fn):
    end=time.monotonic()+15
    while time.monotonic()<end:
        result=fn()
        if result:return result
        time.sleep(.03)
    raise AssertionError('deadline')
def closed(port):
    try:
        with socket.create_connection(('127.0.0.1',port),.2):return False
    except ConnectionRefusedError:return True
with tempfile.TemporaryDirectory(prefix='production-gateway-fixture-',dir=build) as tmp:
    for fault in ['corrupt','service','crash','write']:
        d=Path(tmp)/fault;nv=d/'security/rng';nv.mkdir(parents=True,mode=0o700);(d/'security').chmod(0o700)
        r=subprocess.run([build/'4vrs-rng','production-enable',nv,'--fresh-entropy-confirmed'],input=bytes(range(32)),capture_output=True);assert r.returncode==0
        if fault=='quota':
            # Build eight real commits through clean owners using fixture helpers.
            import importlib.util
            import socket,struct
            for _ in range(7):
                if _:assert subprocess.run([build/'4vrs-rng','service-arm',nv,'--preserve-quota'],capture_output=True).returncode==0
                a,x=socket.socketpair();b,y=socket.socketpair();pid=os.fork()
                if not pid:
                    xx=os.dup(x.fileno());yy=os.dup(y.fileno());os.dup2(xx,3);os.dup2(yy,4);os.closerange(5,1024);os.execl(build/'4vrs-rng','4vrs-rng','serve',str(nv))
                x.close();y.close();a.settimeout(5);a.sendall(b'RNG1'+struct.pack('!III',1,32,0));assert len(a.recv(48))==48
                os.kill(pid,signal.SIGTERM);assert os.waitstatus_to_exitcode(os.waitpid(pid,0)[1])==0;a.close();b.close()
        elif fault=='corrupt':
            data=bytearray((nv/'state').read_bytes());data[100]^=1;(nv/'state').write_bytes(data)
        elif fault=='service':(nv/'attempt').write_bytes(b'production-v1\n');(nv/'attempt').chmod(0o600)
        with (d/'fixture.log').open('w') as log:
            env=dict(os.environ,WEB_FIXTURE_HTTP='28080',WEB_FIXTURE_HTTPS='28443')
            if fault=='write':env['NV_FAIL']='before-write'
            proc=subprocess.Popen([build/'gateway-host',d,build/'4vrs-web'],stdout=log,stderr=log,env=env)
            def command(value):
                f=d/'local-command.next';f.write_text(value);f.replace(d/'local-command');wait(lambda:not (d/'local-command').exists())
            def status():
                command('status');return json.loads((d/'web-state').read_text())
            def ports():
                command('port-status');return json.loads((d/'port-state').read_text())
            try:
                time.sleep(.4);before=ports();command('http');command('enable')
                if fault=='crash':
                    s=wait(lambda: (v if (v:=status())['state']==2 else None))
                    os.kill(s['rng_pid'],signal.SIGKILL)
                expected={'quota':37,'corrupt':34,'service':38,'crash':39,'write':38}[fault]
                s=wait(lambda:(v if (v:=status())['state']==3 and v['rng_pid']==0 else None))
                assert s['error']==expected,(fault,s)
                state=(nv/'state').read_bytes();command('timeout');time.sleep(.2);later=status()
                assert later['ticks']>s['ticks'] and later['gateway']==2 and later['rng_pid']==0
                assert (nv/'state').read_bytes()==state and ports()==before
                assert before['active']==8 and closed(28080) and closed(28443)
                print('PASS',fault,'Web closed; eight mock transports unchanged; Gateway ticks continue; no retry after 70s',flush=True)
            finally:
                proc.terminate();proc.wait(timeout=15)
