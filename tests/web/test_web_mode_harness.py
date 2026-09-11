"""Offline profile and staged request logging tests, socketpair only."""
import importlib.util,json,socket,threading,time,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('h',root/'tools/web-mode-harness.py');h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h)
with tempfile.TemporaryDirectory() as d:
    d=Path(d);known=d/'known_hosts';known.write_text('');log=d/'run.jsonl'
    c=json.loads((root/'tools/web-mode-profile.example.json').read_text())
    try:h.validate(c)
    except ValueError:pass
    else:raise AssertionError('Incomplete profile accepted')
    c.update(ssh_user='fixture',known_hosts=str(known),authorization_reference='local-test-only')
    h.validate(c)  # Web-only, no invented Modbus registers or TLS pin for HTTP.
    c['protocol']='https'
    try:h.validate(c)
    except ValueError:pass
    else:raise AssertionError('HTTPS without public pin accepted')
    c['protocol']='http';a,b=socket.socketpair();real=h.socket.create_connection
    def peer():
        try:
            for i in range(2):
                request=b''
                while b'\r\n\r\n' not in request:request+=b.recv(1024)
                assert request.startswith(b'GET /') and b'Cookie:' not in request
                b.sendall(b'H');time.sleep(.01);b.sendall(b'TTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc')
        finally:b.close()
    t=threading.Thread(target=peer);t.start();h.socket.create_connection=lambda *args,**kwargs:a
    try:assert h.measure(c,log,1,['/','/api/hello'],time.monotonic()+2,'reuse')
    finally:h.socket.create_connection=real;t.join(2)
    rows=[json.loads(x) for x in log.read_text().splitlines()];req=[r for r in rows if r['event']=='request']
    assert len(req)==2 and [r['reused_connection'] for r in req]==[False,True]
    assert all(r['bytes']==3 and r['headers_s']>=r['first_byte_s'] for r in req)
    assert all('body' not in r and 'cookie' not in r for r in rows)
    a,b=socket.socketpair();h.socket.create_connection=lambda *args,**kwargs:a
    try:assert not h.measure(c,log,2,['/'],time.monotonic()+.05,'cold')
    finally:h.socket.create_connection=real;b.close()
    last=json.loads(log.read_text().splitlines()[-1]);assert last['event']=='web_error' and last['failed_stage']=='first_byte' and last['connection']==2
spec=importlib.util.spec_from_file_location('summary',root/'tools/summarize-web-modes.py');summary=importlib.util.module_from_spec(spec);spec.loader.exec_module(summary)
result=summary.summarize([{'event':'request','scenario':'reuse','status':200,'complete_s':.2},{'event':'web_error','scenario':'reuse','failed_stage':'tls','error':'TimeoutError'},{'event':'load_stopped','scenario':'reuse'},{'event':'end'}])
assert result['complete'] and result['scenarios']['reuse']['completed']==1 and result['scenarios']['reuse']['errors_by_stage']=={'tls:TimeoutError':1}
assert result['scenarios']['reuse']['latency_s']['95']==.2 and len(result['load_boundaries'])==1
print('Web-only profiles, HTTPS pin requirement, per-request first-byte/headers/body, reuse, timeout stages and offline summary PASS')
