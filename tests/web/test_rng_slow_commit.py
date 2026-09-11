"""Real Gateway/client/broker, host-only fsync delay; public disposable seeds."""
from pathlib import Path
import os,sys,subprocess,tempfile,time,json,re,signal,socket
build=Path(sys.argv[1]).resolve();probe=Path(sys.argv[2]).resolve();mode=sys.argv[3];report=Path(sys.argv[4])
def wait(fn,seconds=45):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.025)
    raise AssertionError('deadline')
def closed(port):
    try:
        with socket.create_connection(('127.0.0.1',port),.1):return False
    except ConnectionRefusedError:return True
cases=[('pending',4500,'timeout'),('witness-next',4500,'timeout')] if mode=='baseline' else [('pending',4500,'success'),('witness-next',4500,'success'),('pending',4500,'signal'),('pending',32000,'deadline'),('preexec',4500,'early-signal')]
results=[]
with tempfile.TemporaryDirectory(prefix='slow-public-fixture-',dir=build) as tmp:
    for index,(file,delay,expected) in enumerate(cases):
        d=Path(tmp)/str(index);nv=d/'security/rng';nv.mkdir(parents=True,mode=0o700);(d/'security').chmod(0o700)
        assert subprocess.run([build/'4vrs-rng','production-enable',nv,'--fresh-entropy-confirmed'],input=bytes(range(32)),capture_output=True).returncode==0
        logpath=d/'fixture.log'
        with logpath.open('w') as log:
            env=dict(os.environ,LD_PRELOAD=str(probe),RNG_SLOW_FILE=file,RNG_SLOW_MS=str(delay),WEB_FIXTURE_HTTP='28080',WEB_FIXTURE_HTTPS='28443')
            proc=subprocess.Popen([build/'gateway-host',d,build/'4vrs-web'],stdout=log,stderr=log,env=env)
            def text():return logpath.read_text()
            def command(value):
                p=d/'command.next';p.write_text(value);p.replace(d/'local-command');wait(lambda:not (d/'local-command').exists(),5)
            def status():command('status');return json.loads((d/'web-state').read_text())
            def ports():command('port-status');return json.loads((d/'port-state').read_text())
            try:
                time.sleep(.3);before=ports();command('http');command('enable')
                match=wait(lambda:re.search(r'probe slow-begin pid=(\d+) ms=(\d+) term-handler=(\d) term-blocked=(\d)',text()))
                pid,start,caught,blocked=map(int,match.groups());at=status();time.sleep(.4);later=status()
                assert blocked==(0 if mode=='baseline' else 1)
                assert later['ticks']>at['ticks'] and ports()==before and before['active']==8
                assert 'rng-allocation' not in text() and closed(28080) and closed(28443)
                if expected in ['signal','early-signal']:os.kill(pid,signal.SIGTERM)
                if expected=='success':
                    wait(lambda:status()['state']==2)
                    assert caught==1 and 'probe slow-end' in text() and 'rng-allocation' in text()
                    assert not re.search(r'probe kill pid='+str(pid)+r' sig=15',text())
                    assert (nv/'state').read_bytes()==(nv/'witness').read_bytes() and not (nv/'pending').exists() and not (nv/'witness-next').exists()
                    final='web-ready';elapsed=int(re.search(r'probe slow-end pid=\d+ ms=(\d+)',text()).group(1))-start
                else:
                    wait(lambda:status()['rng_pid']==0)
                    trace=text();reaped=re.search(r'probe reaped pid='+str(pid)+r' signal=(\d+) exit=(-?\d+) ms=(\d+)',trace);assert reaped,trace[-1000:]
                    death,exitcode,ended=map(int,reaped.groups());elapsed=ended-start
                    state=subprocess.run([build/'4vrs-rng','status',nv],capture_output=True)
                    if mode=='baseline':
                        assert caught==0 and death==15 and 'rng-client-fail' in trace and 'probe slow-end' not in trace and state.returncode==8
                        assert 2500<=elapsed<delay
                        final='SIGTERM-during-unhandled-commit-service-required'
                    else:
                        assert caught==(0 if expected=='early-signal' else 1) and death==0 and exitcode==0 and state.returncode==0 and elapsed>=delay
                        assert (nv/'state').read_bytes()==(nv/'witness').read_bytes() and 'rng-allocation' not in trace
                        final='commit-completed-without-output'
                        if expected=='early-signal':
                            assert int.from_bytes((nv/'state').read_bytes()[8:12],'big')==1
                            final='pending-stop-before-main-no-NV-write'
                    stable={p.name:p.read_bytes() for p in nv.iterdir() if p.is_file()};command('timeout');time.sleep(.2)
                    assert status()['rng_pid']==0 and stable=={p.name:p.read_bytes() for p in nv.iterdir() if p.is_file()}
                assert ports()==before and status()['gateway']==2
                results.append(dict(file=file,delay_ms=delay,elapsed_ms=elapsed,term_handler=caught,term_blocked=blocked,result=final,gateway_continues=True,mock_ports=8))
                print('PASS',mode,file,expected,elapsed,'ms',flush=True)
            finally:proc.terminate();proc.wait(timeout=45)
report.write_text(json.dumps({'mode':mode,'scope':'host real Gateway/client/broker; injected delay before fsync, not physical CF','cases':results},indent=2)+'\n')
