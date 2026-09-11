"""Repeatable host-only baseline/candidate comparison; never connects to hardware."""
from pathlib import Path
import sys
source=Path(sys.argv[2]);helpers=(source/'tests/web/test_web_integration.py').read_text()
exec(compile(helpers[:helpers.index('\ntry:\n start();')],str(source/'tests/web/test_web_integration.py'),'exec'))
import concurrent.futures,statistics
def processes():
 result={}
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   a=(p/'stat').read_text().rsplit(')',1)[1].split();ppid=int(a[1]);pid=int(p.name)
   if pid!=process.pid and ppid!=process.pid:continue
   result[pid]={'cpu':int(a[11])+int(a[12]),'children_cpu':int(a[13])+int(a[14]),'rss_kib':int(a[21])*4,'nice':int(a[16]),'comm':(p/'comm').read_text().strip()}
  except (FileNotFoundError,ProcessLookupError):pass
 return result
def delta(before,after):
 return {v['comm']:{'cpu_ticks':v['cpu']-before.get(k,v)['cpu'],'reaped_child_cpu_ticks':v['children_cpu']-before.get(k,v)['children_cpu'],'rss_kib':v['rss_kib'],'nice':v['nice']} for k,v in after.items()}
def phase(clients,reuse,warm,path,tag):
 before=processes();begin=time.monotonic()
 def worker(_):
  c=None;samples=[];errors=0;handshakes=0;body_bytes=0;codes={}
  try:
   for i in range(20):
    if c is None:c=http.client.HTTPSConnection(ip,18443,context=context,timeout=15)
    if c.sock is None:handshakes+=1
    headers={'Cookie':cookie} if path.startswith('/api/') else {}
    if warm and tag:headers['If-None-Match']=tag
    at=time.monotonic()
    try:
     c.request('GET',path,headers=headers);r=c.getresponse();b=r.read();codes[r.status]=codes.get(r.status,0)+1;body_bytes+=len(b)
     if r.status not in (200,304):errors+=1
    except (OSError,http.client.HTTPException):errors+=1;c.close();c=None
    samples.append(time.monotonic()-at)
    if not reuse and c:c.close();c=None
  finally:
   if c:c.close()
  return samples,errors,handshakes,body_bytes,codes
 with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as pool:results=list(pool.map(worker,range(clients)))
 samples=sorted(x for r in results for x in r[0]);after=processes()
 return {'clients':clients,'reuse_requested':reuse,'warm_requested':warm,'validator_available':bool(tag),'path':path,'requests':len(samples),'handshakes':sum(r[2] for r in results),'response_body_bytes':sum(r[3] for r in results),'errors':sum(r[1] for r in results),'p50_s':statistics.median(samples),'p95_s':samples[int((len(samples)-1)*.95)],'p99_s':samples[-1],'wall_s':time.monotonic()-begin,'processes':delta(before,after)}
try:
 start();command('enable');wait(lambda:request('/api/hello',auth=False)[0]==200)
 code=wait(lambda:(root/'display').read_text() if (root/'display').exists() else '')
 before=processes();at=time.monotonic();api('enroll',{'password':'public-benchmark-password','code':code},auth=False)
 enrollment={'seconds':time.monotonic()-at,'processes':delta(before,processes())}
 api('logout',{});before=processes();at=time.monotonic();api('login',{'password':'public-benchmark-password'},auth=False)
 login={'seconds':time.monotonic()-at,'processes':delta(before,processes())}
 tag=request('/app.js',auth=False)[2].get('ETag');results=[]
 for clients in [1,2]:
  for reuse,warm in [(False,False),(True,False),(True,True)]:results.append(phase(clients,reuse,warm,'/app.js',tag))
  results.append(phase(clients,True,False,'/api/system',None))
 result={'scope':'host only, identical loopback fixture; process cpu plus reaped helper cpu reported separately; RSS endpoints not leak proof','clock_ticks_per_second':os.sysconf('SC_CLK_TCK'),'enrollment':enrollment,'login':login,'phases':results}
 (build/'benchmark-results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result),flush=True)
finally:
 stop();log.close()
 if (build/'benchmark-results.json').exists():
  result=json.loads((build/'benchmark-results.json').read_text())
  result['web_budget_peak_between_checks_us']=list(map(int,re.findall(r'web-budget peak_between_checks_us=(\d+)',(root/'test.log').read_text())))
  (build/'benchmark-results.json').write_text(json.dumps(result,indent=2)+'\n')
