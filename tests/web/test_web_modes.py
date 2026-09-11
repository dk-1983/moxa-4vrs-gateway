"""Real host HTTP/TLS/IPC, strict one-socket admission and protocol rollback."""
from pathlib import Path
import sys
source=Path(sys.argv[2]);helper=(source/'tests/web/test_web_integration.py').read_text()
exec(compile(helper[:helper.index('\ntry:\n start();')],str(source/'tests/web/test_web_integration.py'),'exec'))

def call(path='hello',values=None,tls=False,token='',cross_origin=False,conn=None):
    c=conn or (http.client.HTTPSConnection(ip,18443,context=context,timeout=12) if tls else http.client.HTTPConnection(ip,18080,timeout=12))
    headers={'Host':ip+(':'+str(18443 if tls else 18080))}
    if token:headers['Cookie']=token
    body=None
    if values is not None:
        body=''.join(k+'='+quote(str(v),safe='')+'\n' for k,v in values.items())
        headers.update({'Origin':('https' if tls or cross_origin else 'http')+'://'+headers['Host'],'Content-Type':'text/plain;charset=UTF-8'})
        if csrf:headers['X-CSRF-Token']=csrf
    c.request('POST' if values is not None else 'GET','/api/'+path,body,headers)
    r=c.getresponse();b=r.read();h=dict(r.getheaders())
    if not conn:c.close();time.sleep(.03)
    return r.status,json.loads(b),h
def state():
    path=root/'web-state';before=path.stat().st_mtime_ns if path.exists() else None
    command('status');wait(lambda:path.exists() and path.stat().st_mtime_ns!=before,5)
    return json.loads(path.read_text())
def ready(protocol):return wait(lambda:(s if (s:=state())['state']==2 and s['protocol']==protocol and not s['switch_pending'] else None),85)
def login(tls,password='test-password-123'):
    global csrf
    s,b,h=call('login',{'password':password},tls);assert s==200,(s,b)
    csrf=b['csrf'];return h['Set-Cookie'].split(';')[0],h
def denied(port,tls):
    try:
        s=socket.create_connection((ip,port),timeout=2);s.settimeout(2)
        if tls:
            try:context.wrap_socket(s,server_hostname=ip).close();return False
            except (OSError,ssl.SSLError):return True
        try:s.sendall(b'GET / HTTP/1.1\r\nHost: 127.0.0.1:18080\r\n\r\n');return s.recv(512)==b''
        except OSError:return True
        finally:s.close()
    except OSError:return True
try:
    start();command('enable');ready(1)
    check('legacy config remains HTTPS; no hidden HTTP',closed(ip,18080))
    original_cert=(root/'security/tls.current').read_bytes()
    command('http');ready(0)
    check('HTTP selected; TLS port closed; certificate retained',closed() and (root/'security/tls.current').read_bytes()==original_cert)
    command('untrusted');check('HTTP independent of trusted TLS clock',call()[0]==200)
    command('code');code=wait(lambda:(root/'display').read_text())
    s,b,h=call('enroll',{'password':'test-password-123','code':code});check('HTTP enrollment works',s==200)
    token=h['Set-Cookie'].split(';')[0];csrf=b['csrf']
    check('HTTP cookie has HttpOnly/SameSite, no Secure',token.startswith('session_http=') and '; Secure' not in h['Set-Cookie'] and 'HttpOnly' in h['Set-Cookie'] and 'SameSite=Strict' in h['Set-Cookie'])
    check('HTTP is authenticated',call('system')[0]==401 and call('system',token=token)[0]==200)
    d=call('system',token=token)[1]
    check('HTTP wrong Origin refused',call('backlight',{'enabled':0,'revision':d['revision']},token=token,cross_origin=True)[0]==403)
    held=http.client.HTTPConnection(ip,18080,timeout=8);call(conn=held)
    check('active HTTP remains, new HTTP refused',denied(18080,False) and call(conn=held)[0]==200)
    held.close();time.sleep(.1)
    # Slow/preconnect occupies the only slot but has a bounded first-byte idle.
    silent=socket.create_connection((ip,18080));time.sleep(.05)
    check('preconnect owns one slot',denied(18080,False));time.sleep(2.1);silent.settimeout(1)
    check('preconnect deadline releases slot',silent.recv(1)==b'');silent.close();check('HTTP resumes after idle',call()[0]==200)
    command('trusted')
    # Certificate failure must not block HTTP; failed HTTPS selection rolls back.
    (root/'security/tls.current').write_bytes(b'invalid test certificate\n');(root/'security/tls.current').chmod(0o600)
    check('HTTP ignores unusable certificate',call()[0]==200)
    d=call('system',token=token)[1]
    check('protocol change accepted once',call('web',{'enabled':1,'interface':0,'protocol':1,'revision':d['revision']},token=token)[0]==202)
    wait(lambda:state()['protocol']==1,5);ready(0)
    check('certificate launch failure rolls back saved mode',b'web.protocol=0\n' in (root/'gateway.conf').read_bytes() and call('system',token=token)[0]==401)
    (root/'security/tls.current').write_bytes(original_cert);(root/'security/tls.current').chmod(0o600)
    token,_=login(False)
    # Listener bind failure also restores the previous working protocol.
    blocker=socket.socket();blocker.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);blocker.bind((ip,18443));blocker.listen(1)
    command('https');wait(lambda:state()['protocol']==1,5);ready(0);blocker.close()
    check('HTTPS bind failure rolls back HTTP',call()[0]==200)
    token,_=login(False);command('https');ready(1)
    check('captured HTTP token cannot authorize HTTPS',call('system',tls=True,token='session='+token.split('=')[1])[0]==401)
    secure_token,h=login(True)
    check('HTTPS Secure cookie restored',secure_token.startswith('session=') and '; Secure;' in h['Set-Cookie'])
    check('old HTTP cookie does not shadow HTTPS cookie',call('system',tls=True,token=token+'; '+secure_token)[0]==200)
    warm=http.client.HTTPSConnection(ip,18443,context=context,timeout=10);call(tls=True,conn=warm)
    check('warm TLS never evicted by cold TLS',all(denied(18443,True) for _ in range(4)) and call(tls=True,conn=warm)[0]==200)
    warm.close();time.sleep(.1)
    # A handshake already owns the sole global slot before any HTTP.
    silent=socket.create_connection((ip,18443));time.sleep(.05)
    check('handshake slot refuses additional TLS',denied(18443,True));silent.close();time.sleep(.1)
    command('both');ip='127.0.0.2';wait(lambda:call(tls=True)[0]==200,85);ip='127.0.0.1'
    warm=http.client.HTTPSConnection(ip,18443,context=context,timeout=10);call(tls=True,conn=warm)
    saved_ip=ip;ip='127.0.0.2';check('one global slot across both LAN listeners',denied(18443,True));ip=saved_ip
    check('existing LAN1 connection survives LAN2 refusal',call(tls=True,conn=warm)[0]==200);warm.close();time.sleep(.1)
    secure_token,_=login(True);s,b,h=call('logout',{},True,secure_token);check('HTTPS logout revokes session',s==200 and call('system',tls=True,token=secure_token)[0]==401)
    command('http');ready(0);token,_=login(False)
    call('logout',{},False,token);check('HTTP logout revokes session',call('system',token=token)[0]==401)
    command('recover');recovery=wait(lambda:(root/'display').read_text());s,_,_=call('enroll',{'password':'changed-test-password','code':recovery});check('HTTP local-confirmed recovery',s==200)
    stop();start();ready(0);token,_=login(False,'changed-test-password');check('saved HTTP and verifier survive restart',call('system',token=token)[1]['data']['web_protocol']==0)
    check('HTTP restart did not touch existing certificate',(root/'security/tls.current').read_bytes()!=b'invalid test certificate\n')
    stop();log.close();root=Path(tempfile.mkdtemp(prefix='web-http-fresh-'));log=open(root/'test.log','w')
    start();command('http');command('enable');ready(0)
    check('fresh HTTP starts without creating any certificate',not (root/'security/tls.current').exists() and call()[0]==200)
finally:
    stop();log.close()
    (build/'mode-results.json').write_text(json.dumps({'passed':passed,'scope':'host fake UART/network; no hardware','fixture':str(root)},indent=2)+'\n')
