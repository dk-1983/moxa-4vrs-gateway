import hashlib
"""Real TLS -> HTTP -> peer-checked IPC -> application; fake UART/network only.
Run inside the isolated Linux build container, never against device addresses.
"""
import hashlib,http.client,json,os,signal,socket,ssl,subprocess,sys,tempfile,time,re
from pathlib import Path
from urllib.parse import quote
build=Path(sys.argv[1]);source=Path(sys.argv[2]);root=Path(tempfile.mkdtemp(prefix='web-integration-'))
assets_source=Path(sys.argv[3]) if len(sys.argv)>3 else source
context=ssl._create_unverified_context();cookie='';csrf='';ip='127.0.0.1';passed=[];metrics={};process=None
def check(name,value):
 assert value,name
 passed.append(name)
 print('PASS',name,flush=True)
def wait(fn,seconds=90):
 end=time.monotonic()+seconds;last=None
 while time.monotonic()<end:
  try:
   last=fn()
   if last:return last
  except (OSError,http.client.HTTPException):pass
  time.sleep(.04)
 raise AssertionError(('wait timed out',last))
def request(path,values=None,headers=None,auth=True,secure=True):
 global cookie,csrf
 metric_start=(root/'test.log').stat().st_size
 h={};body=None
 if auth and cookie:h['Cookie']=cookie
 if values is not None:
  body=''.join(f'{k}={quote(str(v),safe="")}\n' for k,v in values.items()).encode()
  h.update({'Origin':f'https://{ip}:18443','Content-Type':'text/plain;charset=UTF-8'})
  if auth and csrf:h['X-CSRF-Token']=csrf
 h.update(headers or {})
 c=http.client.HTTPSConnection(ip,18443,context=context,timeout=10) if secure else http.client.HTTPConnection(ip,18080,timeout=10)
 c.request('POST' if values is not None else 'GET',path,body,h);r=c.getresponse();data=r.read();rh=dict(r.getheaders());status=r.status;c.close()
 if 'Set-Cookie' in rh:cookie='' if 'Max-Age=0' in rh['Set-Cookie'] else rh['Set-Cookie'].split(';')[0]
 if rh.get('Content-Type')=='application/json':
  data=json.loads(data)
  if data.get('csrf'):csrf=data['csrf']
 with (root/'test.log').open('rb') as sample:
  sample.seek(metric_start);events=sample.read().decode(errors='replace')
 profile=metrics.setdefault('rng_by_route',{}).setdefault(path,{'operations':0,'tls_calls':0,'tls_bytes':0,'gateway_calls':0,'gateway_bytes':0,'web_seed_calls':0,'web_seed_bytes':0,'broker_calls':0,'broker_bytes':0})
 profile['operations']+=1
 for key,pattern in [('tls',r'rng-tls bytes=(\d+)'),('gateway',r'rng-consumer mode=gateway bytes=(\d+)'),('web_seed',r'rng-consumer mode=web bytes=(\d+)'),('broker',r'rng-allocation requests=\d+ bytes=(\d+)')]:
  sizes=list(map(int,re.findall(pattern,events)));profile[key+'_calls']+=len(sizes);profile[key+'_bytes']+=sum(sizes)
 return status,data,rh
def api(op,values=None,expected=200,**kw):
 s,d,h=wait(lambda:request('/api/'+op,values,**kw)) if values is None else request('/api/'+op,values,**kw);
 if s==503 and expected==200:raise OSError('Web is rebinding')
 assert s==expected,(op,s,d);return d
def command(s):
 metric_start=(root/'test.log').stat().st_size
 staged=root/'local-command.next';staged.write_text(s);staged.replace(root/'local-command');wait(lambda:not (root/'local-command').exists())
 with (root/'test.log').open('rb') as sample:
  sample.seek(metric_start);events=sample.read().decode(errors='replace')
 sizes=list(map(int,re.findall(r'rng-consumer mode=gateway bytes=(\d+)',events)))
 if sizes:metrics.setdefault('local_commands',[]).append({'command':s,'gateway_calls':len(sizes),'gateway_bytes':sum(sizes)})
def closed(addr=ip,port=18443):
 try:
  with socket.create_connection((addr,port),timeout=.2):return False
 except ConnectionRefusedError:return True
 except OSError:return False  # full backlog/timeout does not prove shutdown
def start(provision=True):
 global process
 # Explicit isolated fixture provisioning; never a production default/seed.
 nv=root/'security/rng'
 if provision:nv.mkdir(parents=True,mode=0o700,exist_ok=True);(root/'security').chmod(0o700)
 if provision and not (nv/'state').exists():
  p=subprocess.run([build/'4vrs-rng','provision',nv],input=bytes(range(32)),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
  assert p.returncode==0,p.stderr.decode()
  (nv/'trial-policy').write_bytes(b'trial-v1\n');(nv/'trial-policy').chmod(0o600)
 process=subprocess.Popen([build/'gateway-host',root,build/'4vrs-web'],stdout=log,stderr=log)
 wait(lambda:(root/'security').is_dir());assert process.poll() is None
def stop():
 if process and process.poll() is None:process.terminate();process.wait(timeout=15)
def net_values(d):
 v={k:d[k] for k in ['default_lan','automatic_dns','dns_lan']}
 for i,l in enumerate(d['lan']):v.update({k+str(i):x for k,x in l.items()})
 v.update(dns0=d['dns'][0],dns1=d['dns'][1]);return v
log=open(root/'test.log','w')
try:
 start();time.sleep(.2);check('Disabled has no listeners',closed())
 blocker=socket.socket();blocker.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);blocker.bind(('127.0.0.1',18443));blocker.listen(1);command('enable')
 def failed_start():
  command('status');s=json.loads((root/'web-state').read_text());return s if s['state']==3 and s['error']==21 else None
 state=wait(failed_start);check('bind conflict fails closed without stopping Gateway',state['saved']==1 and state['error']==21 and state['gateway']==2 and closed(ip,18080));blocker.close();wait(lambda:request('/api/hello',auth=False)[0]==200)
 code=wait(lambda:(root/'display').read_text() if (root/'display').exists() else '')
 check('local 48-bit code',len(code)==12)
 hello=api('hello',auth=False);check('enrollment only',hello['data']=={'configured':False,'enrollment':True})
 api('overview',expected=401,auth=False);api('ports',expected=401,auth=False);api('network',expected=401,auth=False)
 for route,file,mime in [('/favicon.ico','assets/icons/favicon.ico','image/x-icon'),('/apple-touch-icon.png','assets/icons/favicon-180.png','image/png'),('/app.js','src/web/ui/app.js','text/javascript'),('/extended.js','src/web/ui/extended.js','text/javascript'),('/style.css','src/web/ui/style.css','text/css'),('/','src/web/ui/index.html','text/html')]:
  s,b,h=request(route,auth=False);check('asset '+route,s==200 and hashlib.sha256(b).hexdigest()==next(a['sha256'] for a in json.loads((build/'web-assets.json').read_text()) if a['route']==route) and h['Content-Type'].startswith(mime))
  check('no external asset '+route,route.endswith(('.ico','.png')) or all(x not in b for x in [b'https://cdn.',b'http://cdn.',b'fonts.googleapis',b'unpkg.com']))
 check('HTTPS selected: HTTP has no listener',closed(ip,18080))
 def raw_request(data):
  with context.wrap_socket(socket.create_connection((ip,18443)),server_hostname=ip) as s:
   s.settimeout(7);s.sendall(data);return s.recv(1024)
 check('duplicate Host rejected',b' 400 ' in raw_request(b'GET / HTTP/1.1\r\nHost: 127.0.0.1:18443\r\nHost: 127.0.0.1:18443\r\n\r\n'))
 check('oversized header rejected',b' 431 ' in raw_request(b'GET / HTTP/1.1\r\nHost: 127.0.0.1:18443\r\nX: '+b'a'*5000+b'\r\n\r\n'))
 check('oversized body rejected',b' 400 ' in raw_request(b'POST /api/login HTTP/1.1\r\nHost: 127.0.0.1:18443\r\nOrigin: https://127.0.0.1:18443\r\nContent-Length: 4097\r\n\r\n'))
 begin=time.monotonic();partial=raw_request(b'GET / HTTP/1.1\r\n');elapsed=time.monotonic()-begin
 metrics['partial_request_seconds']=elapsed
 if partial or not 4.5<elapsed<7:print('partial request observation',elapsed,repr(partial),flush=True)
 check('partial request deadline',partial==b'' and 4.5<elapsed<7)
 check('host rejected',request('/api/hello',headers={'Host':'attacker.test'},auth=False)[0]==400)
 check('origin rejected',request('/api/enroll',{'password':'test-password-123','code':code},headers={'Origin':'https://evil'},auth=False)[0]==403)
 api('enroll',{'password':'test-password-123','code':'000000000000'},expected=403,auth=False)
 begin=time.monotonic();api('enroll',{'password':'test-password-123','code':code},auth=False);metrics['host_enrollment_seconds']=round(time.monotonic()-begin,3)
 check('secure cookie flags',cookie.startswith('session=') and len(csrf)==64)
 for route,file in [('/help/en.html','src/web/ui/help-en.html'),('/help/ru.html','src/web/ui/help-ru.html'),('/help/main-menu.png','assets/help/main-menu.png'),('/help/configuration.png','assets/help/configuration.png')]:
  check('private Help '+route,request(route,auth=False)[0]==401)
  status,data,headers=request(route);check('embedded Help '+route,status==200 and hashlib.sha256(data).hexdigest()==next(a['sha256'] for a in json.loads((build/'web-assets.json').read_text()) if a['route']==route))
 system_data=api('system')['data']
 check('certificate fingerprint published',len(system_data['fingerprint'])==64)
 check('real port detail',api('detail1')['data']['id']==1 and 'completed' in api('detail1')['data'])
 api('enroll',{'password':'test-password-123','code':code},expected=403,auth=False)
 command('draft-open');old=api('ports');p=old['data'][0];p['port']=1502
 api('port',{**p,'revision':old['revision']},expected=202)
 wait(lambda:api('ports')['data'][0]['port']==1502);command('draft-save');check('stale panel draft rejected',wait(lambda:(root/'panel-result').read_text() if (root/'panel-result').exists() else '')=='conflict')
 api('port',{**p,'revision':old['revision']},expected=409);check('stale HTTP draft rejected',True)
 current=api('system');command('panel-save');wait(lambda:api('system')['revision']!=current['revision'])
 api('backlight',{'enabled':1,'revision':current['revision']},expected=409);check('panel change invalidates Web draft',True)
 check('CSRF rejected',request('/api/backlight',{'enabled':1,'revision':api('system')['revision']},headers={'X-CSRF-Token':'a'*64})[0]==401)
 # An unrelated process cannot become the Gateway IPC peer.
 peer=socket.socket(socket.AF_UNIX);peer.connect(f'/var/4vrs-web-ipc/{process.pid}.sock');peer.settimeout(2)
 try:peer.sendall(b'4V\x01\0\0\0\0\x09op=hello\n');denied=peer.recv(20)==b''
 except ConnectionResetError:denied=True
 peer.close();check('wrong IPC peer rejected',denied)
 d=api('network');check('real network owner available',d['data']['available']);v=net_values(d['data']);v['address1']='192.168.4.126'
 # Review and Apply both validate through the production network validator.
 baseline=api('network')
 for field,value,code in [('netmask0','255.255.240.350','ipv4'),('netmask0','255.255.255.4','mask'),('address0','10.0.2.350','ipv4'),('gateway0','203.0.113.1','gateway'),('dns0','300.1.1.1','ipv4')]:
  for operation in ('network_review','network_apply'):
   rejected=api(operation,{**v,field:value,'revision':d['revision']},expected=422)
   check(operation+' structured '+field+' '+code,rejected['data']['validation']=={'field':field,'lan':0 if field.startswith('dns') else 1,'code':code})
 valid={**v,'address0':'10.0.2.13','netmask0':'255.254.0.0','gateway0':'10.0.0.1','revision':d['revision']}
 api('network_review',valid);check('valid /15 review has no side effects',api('network')==baseline)
 api('network_review',{**valid,'address1':'10.0.4.13'},expected=422)
 current=api('ports');port=current['data'][0]
 for value in ('-1','65536','1.5','0'):
  rejected=api('port',{**port,'port':value,'enabled':1,'revision':current['revision']},expected=422)
  check('numeric port rejected '+value,rejected['data']['validation']['field']=='port')
 api('network_apply',{**v,'revision':d['revision']},expected=202)
 state=wait(lambda:(s if (s:=api('network')['data'])['keep_allowed'] else None))
 check('Keep countdown',0<state['remaining_seconds']<=60)
 api('keep',{'epoch':'wrong','operation':state['operation']},expected=409)
 api('keep',{'epoch':state['epoch'],'operation':state['operation']},expected=202)
 wait(lambda:api('network')['data']['lan'][1]['address']=='192.168.4.126');check('network Apply Keep',True)
 time.sleep(.3);d=api('network');v['address1']='192.168.4.125';api('network_apply',{**v,'revision':d['revision']},expected=202)
 state=wait(lambda:(s if (s:=api('network')['data'])['keep_allowed'] else None))
 api('revert',{'epoch':state['epoch'],'operation':state['operation']},expected=202)
 wait(lambda:api('network')['data']['state']==9);check('network Revert',api('network')['data']['lan'][1]['address']=='192.168.4.126' and api('network')['data']['rollback_reason']==2)
 time.sleep(.2);d=api('network');api('network_apply',{**v,'revision':d['revision']},expected=202);wait(lambda:api('network')['data']['keep_allowed']);command('timeout')
 wait(lambda:api('network')['data']['state']==9);check('lost client monotonic timeout rollback',api('network')['data']['rollback_reason']==3)
 cert=(root/'security/tls.current').read_bytes();admin=(root/'security/admin').read_bytes();boot=api('system')['revision']
 command('stop-web-fixture');begin=time.monotonic();command('disable');wait(lambda:closed() and closed(port=18080),3);check('Disable terminates even a stopped Web within three seconds',time.monotonic()-begin<3);blocker=socket.socket();blocker.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);blocker.bind(('127.0.0.1',18443));blocker.listen(1);command('enable')
 def failed_start():
  command('status');s=json.loads((root/'web-state').read_text());return s if s['state']==3 and s['error']==21 else None
 state=wait(failed_start);check('bind conflict fails closed without stopping Gateway',state['saved']==1 and state['error']==21 and state['gateway']==2 and closed(ip,18080));blocker.close();wait(lambda:request('/api/hello',auth=False)[0]==200)
 check('credential and TLS generation unchanged',cert==(root/'security/tls.current').read_bytes() and admin==(root/'security/admin').read_bytes())
 api('overview',expected=401);api('login',{'password':'test-password-123'},auth=False)
 command('rebind');ip='127.0.0.3';wait(lambda:request('/api/hello',auth=False)[0]==200,75);check('exact rebind closes old address',closed('127.0.0.1'))
 changed=(root/'security/tls.current').read_bytes();check('certificate follows IP',changed!=cert and b'127.0.0.3,-' in changed)
 api('login',{'password':'test-password-123'},auth=False);api('logout',{});api('overview',expected=401)
 command('panel-home');api('recover',{},expected=202,auth=False);api('recover',{},expected=202,auth=False);check('remote request has no code',not (root/'display').read_text());command('confirm-recovery');newcode=wait(lambda:(c if (c:=(root/'display').read_text()) and c!=code else None));api('login',{'password':'test-password-123'},expected=200,auth=False)
 api('enroll',{'password':'new-test-password-123','code':newcode},auth=False);api('overview',expected=401);api('login',{'password':'new-test-password-123'},auth=False);check('local recovery changes verifier only',admin!=(root/'security/admin').read_bytes() and api('ports')['data'][0]['port']==1502)
 stop();ip='127.0.0.1';cookie=csrf='';start();wait(lambda:request('/api/hello',auth=False)[0]==200)
 check('boot preserves administrator',api('hello',auth=False)['data']['configured']);api('login',{'password':'new-test-password-123'},auth=False)
 api('backlight',{'enabled':1,'revision':boot},expected=409);check('previous boot revision rejected',True)
 check('configuration survived boot',api('ports')['data'][0]['port']==1502)
 # UTF-8 byte policy and the full authenticated asynchronous password change path.
 revision=api('system')['revision'];original=(root/'security/admin').read_bytes()
 api('password',{'current':'new-test-password-123','password':'\u044f'*65,'repeat':'\u044f'*65,'revision':revision},expected=400)
 api('password',{'current':'wrong-password-123','password':'\u044f'*6,'repeat':'\u044f'*6,'revision':revision},expected=403)
 check('refused password change preserves verifier',original==(root/'security/admin').read_bytes())
 api('password',{'current':'new-test-password-123','password':'\u044f'*6,'repeat':'\u044f'*6,'revision':revision})
 api('overview',expected=401);api('login',{'password':'new-test-password-123'},expected=403,auth=False)
 api('login',{'password':'\u044f'*6},auth=False);check('UTF-8 twelve-byte password and session revocation',True)
 stop();cookie=csrf='';start();wait(lambda:request('/api/hello',auth=False)[0]==200)
 api('login',{'password':'\u044f'*6},auth=False);check('changed password survives restart',True)
 api('password',{'current':'\u044f'*6,'password':'new-test-password-123','repeat':'new-test-password-123','revision':api('system')['revision']})
 # Bounded stalled broker must not stop the actual Gateway event-loop fixture.
 command('status');before=json.loads((root/'web-state').read_text());owner=before['rng_pid'];assert owner>0
 os.kill(owner,signal.SIGSTOP)
 try:
  command('rng-drain');begin=time.monotonic();command('status');middle=json.loads((root/'web-state').read_text())
  time.sleep(.3);command('status');after=json.loads((root/'web-state').read_text())
  check('Gateway loop progresses while RNG owner is stopped',after['gateway']==2 and after['ticks']>middle['ticks']+20 and time.monotonic()-begin<1)
 finally:os.kill(owner,signal.SIGCONT)
 wait(lambda:request('/api/hello',auth=False)[0]==200)
 api('login',{'password':'new-test-password-123'},auth=False);old_cookie=cookie
 os.kill(owner,signal.SIGKILL);wait(closed)
 command('timeout');time.sleep(.2);command('status');failed_rng=json.loads((root/'web-state').read_text())
 check('RNG death requires explicit service restart',failed_rng['rng_pid']==0 and failed_rng['gateway']==2)
 stop();start(False);wait(lambda:request('/api/hello',auth=False)[0]==200)
 check('broker death revokes sessions',request('/api/overview',headers={'Cookie':old_cookie})[0]==401)
 stop();cookie=csrf='';policy=root/'security/rng/trial-policy';policy.rename(policy.with_name('policy-held'));start(False);command('status')
 def unavailable(code):
  command('status');v=json.loads((root/'web-state').read_text());return v['gateway']==2 and v['state']==3 and v['error']==code
 wait(lambda:unavailable(35));check('policy gate preserves Gateway',closed())
 policy.with_name('policy-held').rename(policy);stop();start(False);wait(lambda:request('/api/hello',auth=False)[0]==200)
 stop();nv=root/'security/rng';nv.rename(root/'security/rng-held');start(False)
 wait(lambda:unavailable(33));check('missing RNG directory preserves Gateway',closed())
 (root/'security/rng-held').rename(nv);stop();start(False);wait(lambda:request('/api/hello',auth=False)[0]==200)
 api('login',{'password':'new-test-password-123'},auth=False);check('late state and settings recover without provision',api('ports')['data'][0]['port']==1502)
 before_series=metrics['rng_by_route']['/api/overview'].copy()
 for i in range(25):api('overview')
 metrics['overview_series']={k:v-before_series[k] for k,v in metrics['rng_by_route']['/api/overview'].items()}
 check('request series uses integrated RNG',True)
 events=(root/'test.log').read_text();certificates=[]
 for block in events.split('rng-certificate-begin\n')[1:]:
  if 'rng-certificate-end' not in block:continue
  body,end=block.split('rng-certificate-end',1);sizes=list(map(int,re.findall(r'rng-tls bytes=(\d+)',body)));seeds=list(map(int,re.findall(r'rng-consumer mode=web bytes=(\d+)',body)))
  certificates.append({'tls_calls':len(sizes),'tls_bytes':sum(sizes),'seed_calls':len(seeds),'seed_bytes':sum(seeds),'result':end.splitlines()[0].strip()})
 metrics['certificates']=certificates
 result={'passed':passed,'metrics':metrics,'scope':'host production HTTP/IPC with fake UART and network','root':str(root)}
 (build/'integration-results.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
finally:
 if sys.exc_info()[0]:
  try:print('failure state',api('network')['data'],api('system')['data'],flush=True)
  except Exception:pass
 stop();log.close();(build/'integration-gateway.log').write_bytes((root/'test.log').read_bytes())
