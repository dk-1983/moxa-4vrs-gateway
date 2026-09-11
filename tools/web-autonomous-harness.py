"""Bounded desktop-only harness. Network access requires separate target approval.
No password/cookie/body logging, remote writes, configuration changes or UART opens.
Modbus profile MUST name operator-approved read-only registers; defaults do not run.
"""
import argparse,concurrent.futures,datetime,hashlib,http.client,ipaddress,json,os,socket,ssl,struct,subprocess,threading,time
from pathlib import Path
LOCK=threading.Lock()
def emit(path,kind,**data):
 with LOCK, path.open('a',encoding='utf8') as f:f.write(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'event':kind,**data})+'\n')
def validate(c):
 def require(ok):
  if not ok:raise ValueError('Incomplete or unsafe profile; fill approved target/register/baseline details')
 require(c['target']=='10.0.2.13' and c['ssh_port']==622)
 require(c['approved_mac']=='00:90:E8:1F:4C:F1' and c['approved_cf_uuid']=='2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52')
 require(len(c['certificate_sha256'])==64 and all(x in '0123456789abcdef' for x in c['certificate_sha256']))
 require(Path(c['known_hosts']).is_file() and c['ssh_user'] and c['authorization_reference'])
 m=c['modbus'];require(m['approved_register_reference'] and m['function'] in (3,4))
 for key,lo,hi in [('port',1,65535),('unit',0,255),('address',0,65535),('count',1,125)]:require(isinstance(m[key],int) and lo<=m[key]<=hi)
 require(m['address']+m['count']<=65536 and .1<=m['period_s']<=60 and .1<=m['timeout_s']<=10)
 require(10<=c['sample_period_s']<=60 and 1<=c['web_clients']<=2)
 require(len(c['phases'])==3 and [p['name'] for p in c['phases']]==['baseline','user-only','mixed'])
 require(all(20<=p['seconds']<=300 for p in c['phases']) and sum(p['seconds'] for p in c['phases'])<=900)
def ssh(c,command):
 args=['ssh.exe' if os.name=='nt' else 'ssh','-p','622','-o','StrictHostKeyChecking=yes','-o','BatchMode=yes','-o','ConnectTimeout=5','-o','UserKnownHostsFile='+c['known_hosts'],c['ssh_user']+'@10.0.2.13',command]
 return subprocess.run(args,capture_output=True,timeout=10)
def exact(sock,n,deadline):
 b=b''
 while len(b)<n:
  remaining=deadline-time.monotonic()
  if remaining<=0:raise TimeoutError('Modbus deadline')
  sock.settimeout(remaining);p=sock.recv(n-len(b))
  if not p:raise ConnectionError('EOF')
  b+=p
 return b
def modbus(c,stop,log,phase):
 m=c['modbus'];tid=0
 while not stop.is_set():
  at=time.monotonic();error=None
  try:
   with socket.create_connection((c['target'],m['port']),timeout=m['timeout_s']) as s:
    tid=(tid+1)&65535;s.sendall(struct.pack('>HHHBBHH',tid,0,6,m['unit'],m['function'],m['address'],m['count']))
    deadline=at+m['timeout_s'];rt,protocol,length,unit=struct.unpack('>HHHB',exact(s,7,deadline))
    if rt!=tid or protocol or unit!=m['unit'] or not 3<=length<=254:raise ValueError('MBAP mismatch')
    b=exact(s,length-1,deadline)
    if b[0]==m['function']|128:raise ValueError('Modbus exception '+str(b[1]))
    if b[0]!=m['function'] or b[1]!=m['count']*2 or len(b)!=2+m['count']*2:raise ValueError('PDU mismatch')
    # Register values deliberately discarded, never logged.
  except (OSError,ValueError) as e:error=type(e).__name__+':'+str(e)
  emit(log,'modbus',phase=phase,seconds=time.monotonic()-at,error=error)
  stop.wait(max(0,m['period_s']-(time.monotonic()-at)))
def web(c,stop,log,client_id):
 ctx=ssl._create_unverified_context();conn=None
 # Exact public certificate pin is checked before every connection sends HTTP.
 while not stop.is_set():
  at=time.monotonic();new=conn is None
  try:
   if conn is None:
    conn=http.client.HTTPSConnection(c['target'],443,context=ctx,timeout=8);conn.connect()
    if hashlib.sha256(conn.sock.getpeercert(binary_form=True)).hexdigest()!=c['certificate_sha256']:raise ValueError('certificate pin mismatch')
   conn.request('GET','/');r=conn.getresponse();r.read(65537)
   emit(log,'web',phase='mixed',client=client_id,new_tls=new,status=r.status,seconds=time.monotonic()-at)
  except (OSError,ValueError,http.client.HTTPException) as e:
   emit(log,'web_error',phase='mixed',client=client_id,seconds=time.monotonic()-at,error=type(e).__name__+':'+str(e))
   if conn:conn.close()
   conn=None
  stop.wait(.75)
 if conn:conn.close()
def run(c,log):
 validate(c)
 # Read-only fresh MAC check; CF UUID must be confirmed by desktop before approval.
 p=ssh(c,'cat /sys/class/net/eth0/address')
 if p.returncode or p.stdout.decode().strip().upper()!=c['approved_mac']:raise ValueError('identity mismatch/unavailable')
 emit(log,'begin',scope='Authorized read-only measurements; no config changes; collector SSH overhead included',authorization=c['authorization_reference'])
 for phase in c['phases']:
  emit(log,'phase',name=phase['name']);stop=threading.Event();end=time.monotonic()+phase['seconds']
  threads=[threading.Thread(target=modbus,args=(c,stop,log,phase['name']),daemon=True)]
  if phase['name']=='mixed':threads += [threading.Thread(target=web,args=(c,stop,log,i),daemon=True) for i in range(c['web_clients'])]
  for t in threads:t.start()
  try:
   while time.monotonic()<end:
    at=time.monotonic()
    try:
     p=ssh(c,"head -n 1 /proc/stat; for f in /proc/[0-9]*/stat; do read line < $f; case \"$line\" in *'(4vrs-web)'*|*'(4vrs-gateway)'*|*'(4vrs-kdf)'*|*'(4vrs-rng)'*) echo \"$line\";; esac; done")
     emit(log,'resources',phase=phase['name'],exit=p.returncode,seconds=time.monotonic()-at,stat=p.stdout.decode(errors='replace')[:8192])
    except (OSError,subprocess.TimeoutExpired) as e:emit(log,'collector_error',phase=phase['name'],error=type(e).__name__)
    stop.wait(max(0,min(c['sample_period_s'],end-time.monotonic())))
  finally:
   stop.set()
   deadline=time.monotonic()+12
   for t in threads:t.join(timeout=max(0,deadline-time.monotonic()))
   if any(t.is_alive() for t in threads):emit(log,'end',reason='worker deadline; no next phase');return
 emit(log,'end')
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--profile',type=Path);p.add_argument('--log',type=Path,required=True);p.add_argument('--validate-only',action='store_true');p.add_argument('--mark',choices=['vpn-change','P7-disabled','P8-disabled','backlight-off','backlight-on','login','navigation','network-scenario-separately-approved']);a=p.parse_args()
 if a.mark:emit(a.log,'operator_action',action=a.mark)
 else:
  c=json.loads(a.profile.read_text(encoding='utf8'));validate(c)
  if a.validate_only:print('Profile valid; no connection made')
  else:run(c,a.log)
