"""Fixed host matrix, failures retained; never hardware or a serial throughput test."""
from pathlib import Path
import sys
source=Path(sys.argv[2]);helpers=(source/'tests/web/test_web_integration.py').read_text()
exec(compile(helpers[:helpers.index('\ntry:\n start();')],str(source/'tests/web/test_web_integration.py'),'exec'))
import concurrent.futures,statistics,hashlib,math
mode=sys.argv[3];label=sys.argv[4];secure=mode=='https';port=18443 if secure else 18080
def connection():return http.client.HTTPSConnection(ip,port,context=context,timeout=18) if secure else http.client.HTTPConnection(ip,port,timeout=18)
def one(c,path,values=None,token=''):
 h={'Host':f'{ip}:{port}'};body=None
 if token:h['Cookie']=token
 if values is not None:body=''.join(k+'='+quote(str(v),safe='')+'\n' for k,v in values.items());h.update(Origin=f'{mode}://{ip}:{port}',**{'Content-Type':'text/plain'})
 c.request('POST' if values is not None else 'GET',path,body,h);r=c.getresponse();b=r.read();return r.status,b,dict(r.getheaders())
def sample():
 result={}
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   f=(p/'stat').read_text().rsplit(')',1)[1].split()
   if int(p.name)!=process.pid and int(f[1])!=process.pid:continue
   result[int(p.name)]={'name':(p/'comm').read_text().strip(),'start':int(f[19]),'cpu':int(f[11])+int(f[12]),'children':int(f[13])+int(f[14]),'rss_kib':int(f[21])*os.sysconf('SC_PAGE_SIZE')//1024,'nice':int(f[16])}
  except (OSError,ValueError):pass
 return result
def delta(before,after):
 return {p['name']:{'cpu_ticks':p['cpu']-before.get(pid,p)['cpu'],'reaped_child_ticks':p['children']-before.get(pid,p)['children'],'rss_kib':p['rss_kib'],'nice':p['nice']} for pid,p in after.items() if pid not in before or p['start']==before[pid]['start']}
def percent(v,q):return sorted(v)[max(0,math.ceil(len(v)*q)-1)] if v else None
rows=[]
try:
 at=time.monotonic();start()
 if not secure:command('http');time.sleep(.1)
 command('enable')
 def available():
  c=connection()
  try:return one(c,'/api/hello')[0]==200
  finally:c.close()
 wait(available);startup=time.monotonic()-at;time.sleep(.1)
 code=wait(lambda:(root/'display').read_text());c=connection();before=sample();at=time.monotonic()
 s,b,h=one(c,'/api/enroll',{'code':code,'password':'public-benchmark-test-123'});assert s==200;c.close();enrollment={'seconds':time.monotonic()-at,'processes':delta(before,sample())};time.sleep(.1)
 c=connection();before=sample();at=time.monotonic();s,b,h=one(c,'/api/login',{'password':'public-benchmark-test-123'});assert s==200;token=h['Set-Cookie'].split(';')[0];c.close();login={'seconds':time.monotonic()-at,'processes':delta(before,sample())};time.sleep(.1)
 for clients in [1,4]:
  for reuse in [False,True]:
   for authenticated in [False,True]:
    paths=['/api/system']*4 if authenticated else ['/','/app.js','/style.css','/api/hello']
    before=sample();at=time.monotonic()
    def worker(index):
     c=None;events=[]
     for path in paths:
      stage='connect';started=time.monotonic();new=c is None
      try:
       if c is None:c=connection();c.connect()
       connected=time.monotonic()-started;stage='request';s,b,h=one(c,path,token=token if authenticated else '')
       events.append({'client':index,'path':path,'status':s,'bytes':len(b),'new_connection':new,'connect_s':connected,'seconds':time.monotonic()-started})
      except (OSError,http.client.HTTPException) as e:
       events.append({'client':index,'path':path,'error':type(e).__name__,'failed_stage':stage,'seconds':time.monotonic()-started})
       if c:c.close()
       c=None
      if not reuse and c:c.close();c=None
      if c is None:time.sleep(.05)
     if c:c.close()
     return events
    with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as pool:events=sum(pool.map(worker,range(clients)),[])
    wall=time.monotonic()-at;cost=delta(before,sample());ok=[r['seconds'] for r in events if 'error' not in r]
    rows.append({'clients':clients,'reuse':reuse,'authenticated':authenticated,'requests':len(events),'completed':len(ok),'errors':len(events)-len(ok),'p50_s':percent(ok,.5),'p95_s':percent(ok,.95),'p99_s':percent(ok,.99),'wall_s':wall,'processes':cost,'events':events})
    time.sleep(.15)
finally:
 stop();log.close()
result={'label':label,'mode':mode,'clock_ticks_per_second':os.sysconf('SC_CLK_TCK'),'scope':'host only; fixed 4 requests/client; normal browser bundles differ; errors retained; process and reaped-child CPU separate, not additive blindly','web_binary_sha256':hashlib.sha256((build/'4vrs-web').read_bytes()).hexdigest(),'startup_wall_s':startup,'enrollment':enrollment,'login':login,'phases':rows}
(build/('benchmark-'+label+'-'+mode+'.json')).write_text(json.dumps(result,indent=2)+'\n')
print(label,mode,'fixed matrix completed, errors retained',sum(r['errors'] for r in rows),flush=True)
