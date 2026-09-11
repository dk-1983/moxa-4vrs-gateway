"""Actual TLS/IPC host fixture: connection reuse, cache and client isolation."""
from pathlib import Path
import sys
# Reuse only setup/helpers, not the old test's execution or hardware targets.
source=Path(sys.argv[2]);helpers=(source/'tests/web/test_web_integration.py').read_text()
exec(compile(helpers[:helpers.index('\ntry:\n start();')],str(source/'tests/web/test_web_integration.py'),'exec'))
import concurrent.futures,statistics
def persistent():return http.client.HTTPSConnection(ip,18443,context=context,timeout=15)
def get(c,path='/',headers=None):
 c.request('GET',path,headers=headers or {});r=c.getresponse();b=r.read();return r.status,b,dict(r.getheaders())
def peer():return context.wrap_socket(socket.create_connection((ip,18443),timeout=15),server_hostname=ip)
try:
 start();command('enable');wait(lambda:request('/api/hello',auth=False)[0]==200)
 c=persistent();status,b,h=get(c);fd=c.sock.fileno();tag=h['ETag'];check('public validator and bounded persistent connection',status==200 and h['Cache-Control']=='public, no-cache' and h['Connection']=='keep-alive')
 status,b,h=get(c,headers={'If-None-Match':tag});check('warm cache 304 on same TLS socket',status==304 and not b and c.sock.fileno()==fd)
 saved_socket=c.sock;time.sleep(5.2);status,b,h=get(c,headers={'If-None-Match':tag});check('TLS survives the five-second UI polling interval',status==304 and c.sock is saved_socket)
 for validator in ['W/'+tag,'"other", W/'+tag,'*']:
  status,b,h=get(c,headers={'If-None-Match':validator});check('weak/list/wildcard validator',status==304 and not b)
 status,b,h=get(c,'/api/hello');check('API never publicly cached',status==200 and h['Cache-Control']=='no-store')
 status,b,h=get(c,'/api/overview');check('no authentication inherited across requests',status==401 and h['Cache-Control']=='no-store');c.close();time.sleep(.04)
 time.sleep(.1)
 slow=socket.create_connection((ip,18443),timeout=5);time.sleep(.05)
 begin=time.monotonic()
 try:denied=peer();denied.close();rejected=False
 except (OSError,ssl.SSLError):rejected=True
 check('new TLS rejected while handshake owns slot',rejected and time.monotonic()-begin<2)
 slow.settimeout(17);begin=time.monotonic();check('handshake deadline releases sole slot',slow.recv(1)==b'' and time.monotonic()-begin<16);slow.close();time.sleep(.1)
 c=peer();payload=f'GET / HTTP/1.1\r\nHost: {ip}:18443\r\nConnection: close\r\n\r\n'.encode()
 for piece in [payload[:7],payload[7:29],payload[29:]]:c.sendall(piece);time.sleep(.015)
 r=http.client.HTTPResponse(c);r.begin();check('fragmented request and bounded response framing',r.status==200 and len(r.read())>0);c.close();time.sleep(.04)
 for name,payload in [('pipeline',f'GET / HTTP/1.1\r\nHost: {ip}:18443\r\n\r\n'*2),('unsupported','PUT / HTTP/1.1\r\nHost: '+ip+':18443\r\n\r\n'),('body','GET / HTTP/1.1\r\nHost: '+ip+':18443\r\nContent-Length: 1\r\n\r\nx'),('transfer','POST / HTTP/1.1\r\nHost: '+ip+':18443\r\nTransfer-Encoding: chunked\r\n\r\n')]:
  c=peer();c.sendall(payload.encode());r=http.client.HTTPResponse(c);r.begin();r.read();check(name+' refused and closed',r.status>=400 and r.getheader('Connection')=='close');c.close();time.sleep(.04)
 c=persistent()
 for i in range(32):status,b,h=get(c)
 check('32 request cap',status==200 and h['Connection']=='close');c.close();time.sleep(.04)
 code=wait(lambda:(root/'display').read_text() if (root/'display').exists() else '')
 api('enroll',{'password':'public-test-password-123','code':code},auth=False)
 c=persistent();saved=cookie
 status,b,h=get(c,'/api/overview',{'Cookie':saved});check('authenticated response remains no-store',status==200 and h['Cache-Control']=='no-store')
 status,b,h=get(c,'/help/en.html',{'Cookie':saved});check('authenticated Help remains no-store without ETag',status==200 and h['Cache-Control']=='no-store' and 'ETag' not in h)
 c.request('POST','/api/logout',b'',{'Cookie':saved,'Origin':f'https://{ip}:18443','X-CSRF-Token':csrf,'Content-Type':'text/plain'});r=c.getresponse();r.read();assert r.status==200
 status,b,h=get(c,'/api/overview',{'Cookie':saved});check('session revocation on reused TLS socket',status==401);c.close();time.sleep(.04)
 api('login',{'password':'public-test-password-123'},auth=False)
 state=api('system');before=state['revision'];desired=1-int(state['data']['backlight'])
 c=peer();body=f'enabled={desired}\nrevision={before}\n'.encode()
 payload=(f'POST /api/backlight HTTP/1.1\r\nHost: {ip}:18443\r\nOrigin: https://{ip}:18443\r\nContent-Type: text/plain\r\nCookie: {cookie}\r\nX-CSRF-Token: {csrf}\r\nContent-Length: {len(body)}\r\n\r\n').encode()+body
 c.sendall(payload);time.sleep(.15);c.close();time.sleep(.04)
 changed=api('system');revision_once=changed['revision'];time.sleep(.3)
 check('disconnect mutation applied once without automatic replay',changed['data']['backlight']==desired and revision_once!=before and api('system')['revision']==revision_once)
 command('status');state=json.loads((root/'web-state').read_text());pid=state.get('pid')
 # Locate only this fixture's direct executable, never external processes.
 child=next(int(p.name) for p in Path('/proc').iterdir() if p.name.isdigit() and (p/'cmdline').exists() and str(build/'4vrs-web').encode() in (p/'cmdline').read_bytes() and int((p/'stat').read_text().rsplit(')',1)[1].split()[1])==process.pid)
 def ticks():v=Path(f'/proc/{child}/stat').read_text().rsplit(')',1)[1].split();return int(v[11])+int(v[12])
 c=peer();before=ticks();time.sleep(1);check('idle client does not busy-loop',ticks()-before<=5);c.close();time.sleep(.04)
 raw=socket.create_connection((ip,18443),timeout=10);incoming=ssl.MemoryBIO();outgoing=ssl.MemoryBIO();tls=context.wrap_bio(incoming,outgoing,server_side=False,server_hostname=ip)
 while True:
  try:tls.do_handshake();pending=outgoing.read();raw.sendall(pending) if pending else None;break
  except ssl.SSLWantReadError:
   pending=outgoing.read()
   if pending:raw.sendall(pending)
   incoming.write(raw.recv(16384))
 tls.write(f'GET / HTTP/1.1\r\nHost: {ip}:18443\r\nConnection: close\r\n\r\n'.encode());encrypted=outgoing.read();raw.sendall(encrypted[:3]);before=ticks();time.sleep(.3)
 check('partial TLS record does not busy-loop',ticks()-before<=5);raw.sendall(encrypted[3:]);clear=b''
 while b'\r\n\r\n' not in clear:
  try:clear+=tls.read(16384)
  except ssl.SSLWantReadError:incoming.write(raw.recv(16384))
 check('partial TLS record resumes correctly',clear.startswith(b'HTTP/1.1 200'));raw.close();time.sleep(.1)
 time.sleep(.1);held=persistent();assert get(held)[0]==200
 begin=time.monotonic()
 for i in range(30):
  contender=socket.create_connection((ip,18443),timeout=2);contender.settimeout(2)
  try:assert contender.recv(1)==b''
  except ConnectionResetError:pass
  contender.close()
 check('refusal burst never evicts warm connection',get(held)[0]==200)
 held.close();time.sleep(.1)
 c=persistent();rss=[]
 for i in range(160):
  assert get(c,'/app.js')[0]==200
  if i%20==0:rss.append(int(Path(f'/proc/{child}/stat').read_text().rsplit(')',1)[1].split()[21])*4)
 c.close();time.sleep(.04);check('bounded repeated load RSS plateau and descriptors',max(rss)-min(rss)<=512 and len(list(Path(f'/proc/{child}/fd').iterdir()))<=32)
 measured=[]
 for reuse in [False,True]:
  c=persistent();samples=[];before=ticks();begin=time.monotonic();errors=0
  for i in range(12):
   if not reuse:c=persistent()
   at=time.monotonic();status,b,h=get(c,'/app.js',{'If-None-Match':tag} if False else None);samples.append(time.monotonic()-at)
   if not reuse:c.close();time.sleep(.04)
  c.close();time.sleep(.04);samples.sort();measured.append({'reuse':reuse,'requests':12,'cpu_ticks':ticks()-before,'wall_s':time.monotonic()-begin,'p50_s':statistics.median(samples),'p95_s':samples[-1],'errors':errors})
 (build/'reuse-results.json').write_text(json.dumps({'passed':passed,'samples':measured,'scope':'host only; no hardware or serial proof'},indent=2)+'\n')
 print(json.dumps(measured),flush=True)
finally:
 stop();log.close()
