"""Real Gateway/Web/RNG, same IPC name after SIGKILL, public CF fixture."""
from pathlib import Path
import subprocess,os,sys,time,json,tempfile,signal,http.client
build=Path(sys.argv[1]);out=Path(sys.argv[2]);endpoint_id=os.getpid()+900000
endpoint=Path('/var/4vrs-web-ipc')/(str(endpoint_id)+'.sock')
def wait(fn):
    end=time.monotonic()+20
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.03)
    raise AssertionError('deadline')
with tempfile.TemporaryDirectory(prefix='ipc-gateway-public-',dir=build) as tmp:
    d=Path(tmp);nv=d/'security/rng';nv.mkdir(parents=True,mode=0o700);nv.parent.chmod(0o700)
    assert subprocess.run([build/'4vrs-rng','production-enable',nv,'--fresh-entropy-confirmed'],input=bytes(range(32)),capture_output=True).returncode==0
    records=[]
    for cycle in [0,1,2]:
        if cycle==0:
            endpoint.parent.mkdir(mode=0o711,exist_ok=True)
            with endpoint.open('xb') as f:f.write(b'public foreign-file fixture')
            foreign_inode=endpoint.stat().st_ino
        with (d/('cycle-'+str(cycle)+'.log')).open('w') as log:
            proc=subprocess.Popen([build/'gateway-host',d,build/'4vrs-web'],stdout=log,stderr=log,env=dict(os.environ,WEB_IPC_TEST_ID=str(endpoint_id),WEB_FIXTURE_HTTP='28080',WEB_FIXTURE_HTTPS='28443'))
            def command(s):
                p=d/'local-command.next';p.write_text(s);p.replace(d/'local-command');wait(lambda:not (d/'local-command').exists())
            def status():command('status');return json.loads((d/'web-state').read_text())
            try:
                time.sleep(.3);command('http');command('enable')
                if cycle==0:
                    wait(lambda:status()['error']==4)
                    first=status();time.sleep(.2);last=status()
                    command('port-status');ports=json.loads((d/'port-state').read_text())
                    assert first['gateway']==last['gateway']==2 and last['ticks']>first['ticks'] and ports['active']==8
                    assert subprocess.run([build/'4vrs-rng','status',nv],capture_output=True).returncode==0
                    assert endpoint.stat().st_ino==foreign_inode and endpoint.read_bytes()==b'public foreign-file fixture'
                    denial={'web_error':4,'gateway_continues':True,'active_mock_ports':8,'rng_ready':True,'foreign_file_preserved':True}
                    continue
                wait(lambda:status()['state']==2)
                c=http.client.HTTPConnection('127.0.0.1',28080,timeout=5);c.request('GET','/');r=c.getresponse();assert r.status==200;r.read();c.close()
                command('port-status');ports=json.loads((d/'port-state').read_text());assert ports['active']==8
                assert subprocess.run([build/'4vrs-rng','status',nv],capture_output=True).returncode==0
                assert (nv/'state').read_bytes()==(nv/'witness').read_bytes()
                generation=int.from_bytes((nv/'state').read_bytes()[8:12],'big')
                records.append({'cycle':cycle,'HTTP':200,'active_mock_ports':8,'generation':generation,'same_endpoint_name':True})
                if cycle==1:
                    children=[int(x) for x in Path('/proc/'+str(proc.pid)+'/task/'+str(proc.pid)+'/children').read_text().split()]
                    proc.kill();proc.wait(timeout=10)
                    # A power cut removes the whole process tree, not just Gateway.
                    for child in children:
                        try:os.kill(child,signal.SIGKILL)
                        except ProcessLookupError:pass
                    assert endpoint.is_socket()
                    wait(lambda:str(endpoint) not in Path('/proc/net/unix').read_text())
                    time.sleep(.3)
            except Exception:
                print('fixture cycle',cycle,'status',status(),flush=True)
                print((d/('cycle-'+str(cycle)+'.log')).read_text()[-2000:],flush=True)
                raise
            finally:
                if proc.poll() is None:proc.terminate();proc.wait(timeout=15)
                if cycle==0:
                    assert endpoint.stat().st_ino==foreign_inode and endpoint.read_bytes()==b'public foreign-file fixture'
                    endpoint.unlink()  # Only this test's exact, identity-checked public fixture.
    assert records[1]['generation']==records[0]['generation']+1
    assert not endpoint.exists()
lock=endpoint.with_suffix('.lock');lock.unlink()
out.write_text(json.dumps({'result':'PASS','scope':'host process crash, not physical power cycle','cycles':records,'ipc_denial':denial},indent=2)+'\n')
print('PASS Gateway SIGKILL, stale inode, same endpoint name, HTTP 200, RNG successor and eight mock ports')
