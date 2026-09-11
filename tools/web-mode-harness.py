"""Autonomous read-only Web stage measurements. Never changes device protocol/config.
Requires a separately approved identity/baseline; optional approved Modbus profile.
No HTTP bodies, cookies, credentials, certificates or register values are logged.
"""
import argparse, datetime, hashlib, http.client, importlib.util, io, json, os, socket, ssl, subprocess, threading, time
from pathlib import Path
LOCK=threading.Lock()
def emit(log,event,**data):
    with LOCK,log.open('a',encoding='utf8') as f:
        f.write(json.dumps({'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'event':event,**data})+'\n')
def validate(c):
    if c['target']!='10.0.2.13' or c['ssh_port']!=622 or c['approved_mac']!='00:90:E8:1F:4C:F1' or c['approved_cf_uuid']!='2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52':raise ValueError('Target identity mismatch')
    if not c['authorization_reference'] or not c['ssh_user'] or not Path(c['known_hosts']).is_file():raise ValueError('Approval/user/known_hosts required')
    if c['protocol'] not in ('http','https'):raise ValueError('Choose http or https')
    if c['protocol']=='https' and (len(c['certificate_sha256'])!=64 or any(x not in '0123456789abcdef' for x in c['certificate_sha256'])):raise ValueError('HTTPS requires fresh public certificate SHA256 pin')
    if not 10<=c['sample_period_s']<=60:raise ValueError('Sample period')
    if [p['name'] for p in c['phases']]!=['baseline','user-only','mixed'] or any(not 20<=p['seconds']<=300 for p in c['phases']) or sum(p['seconds'] for p in c['phases'])>600:raise ValueError('Bounded phases required')
    m=c.get('modbus')
    if m is not None:
        if not m.get('approved_register_reference') or m.get('function') not in (3,4):raise ValueError('Approved read-only Modbus profile required')
        for k,lo,hi in [('port',1,65535),('unit',0,255),('address',0,65535),('count',1,125)]:
            if not isinstance(m.get(k),int) or not lo<=m[k]<=hi:raise ValueError('Modbus '+k)
        if m['address']+m['count']>65536 or not .1<=m['period_s']<=60 or not .1<=m['timeout_s']<=10:raise ValueError('Modbus bounds')
def ssh(c,command):
    return subprocess.run(['ssh.exe' if os.name=='nt' else 'ssh','-p','622','-o','StrictHostKeyChecking=yes','-o','BatchMode=yes','-o','ConnectTimeout=5','-o','UserKnownHostsFile='+c['known_hosts'],c['ssh_user']+'@10.0.2.13',command],capture_output=True,timeout=10)
class PrefixReader(io.RawIOBase):
    def __init__(self,prefix,stream):self.prefix=prefix;self.stream=stream
    def readable(self):return True
    def readinto(self,b):
        if self.prefix:
            n=min(len(b),len(self.prefix));b[:n]=self.prefix[:n];self.prefix=self.prefix[n:];return n
        return self.stream.readinto(b)
    def close(self):self.stream.close();super().close()
class PrefixSocket:
    def __init__(self,s,prefix):self.s=s;self.prefix=prefix
    def makefile(self,*args,**kwargs):return io.BufferedReader(PrefixReader(self.prefix,self.s.makefile('rb',buffering=0)))
def measure(c,log,connection,paths,deadline,scenario,barrier=None):
    s=None;timer=None;stage='tcp';begin=time.monotonic()
    if barrier:barrier.wait(timeout=3)
    def timeout():
        remaining=deadline-time.monotonic()
        if remaining<=0:raise TimeoutError('phase deadline')
        return min(18,remaining)
    try:
        s=socket.create_connection((c['target'],443 if c['protocol']=='https' else 80),timeout=timeout())
        # An absolute deadline also bounds trickling response headers/body.
        def expire():
            try:s.shutdown(socket.SHUT_RDWR)
            except OSError:pass
        timer=threading.Timer(max(0,deadline-time.monotonic()),expire);timer.daemon=True;timer.start()
        tcp=time.monotonic()-begin;tls=0
        if c['protocol']=='https':
            stage='tls';at=time.monotonic();ctx=ssl._create_unverified_context();ctx.minimum_version=ssl.TLSVersion.TLSv1_2
            s=ctx.wrap_socket(s,server_hostname=c['target'],do_handshake_on_connect=False);s.settimeout(timeout());s.do_handshake()
            if hashlib.sha256(s.getpeercert(binary_form=True)).hexdigest()!=c['certificate_sha256']:raise ValueError('certificate pin mismatch')
            tls=time.monotonic()-at
        emit(log,'connection',phase='mixed',connection=connection,scenario=scenario,protocol=c['protocol'],tcp_s=tcp,tls_s=tls,new_tcp=True,reconnect_of=None)
        for index,path in enumerate(paths):
            at=time.monotonic();stage='send';s.settimeout(timeout())
            # Host deliberately omits the default port. No credentials or mutations.
            s.sendall(('GET '+path+' HTTP/1.1\r\nHost: '+c['target']+'\r\nConnection: keep-alive\r\n\r\n').encode())
            stage='first_byte';s.settimeout(timeout());first=s.recv(1)
            if not first:raise ConnectionError('EOF before response')
            first_s=time.monotonic()-at;stage='headers';r=http.client.HTTPResponse(PrefixSocket(s,first));r.begin();headers_s=time.monotonic()-at
            stage='body';size=0
            while True:
                s.settimeout(timeout());part=r.read(16384)
                if not part:break
                size+=len(part)
                if size>4*1024*1024:raise ValueError('response size limit')
            closing=r.will_close;status=r.status;r.close()
            emit(log,'request',phase='mixed',connection=connection,scenario=scenario,index=index,path=path,status=status,first_byte_s=first_s,headers_s=headers_s,complete_s=time.monotonic()-at,bytes=size,server_close=closing,reused_connection=index>0)
            if closing:
                if index+1<len(paths):emit(log,'sequence_stopped',phase='mixed',connection=connection,reason='server close; remaining requests not executed')
                break
        return True
    except (OSError,ValueError,http.client.HTTPException) as e:
        emit(log,'web_error',phase='mixed',connection=connection,scenario=scenario,failed_stage=stage,path=locals().get('path'),index=locals().get('index'),seconds=time.monotonic()-begin,error=type(e).__name__)
        return False
    finally:
        if timer:timer.cancel()
        if s:s.close()
def run(c,log):
    validate(c);p=ssh(c,'cat /sys/class/net/eth0/address')
    if p.returncode or p.stdout.decode().strip().upper()!=c['approved_mac']:raise ValueError('Fresh MAC verification failed')
    stop=threading.Event();phase={'name':'baseline','load_active':False}
    def collect():
        while not stop.is_set():
            try:
                p=ssh(c,"head -n 1 /proc/stat; for f in /proc/[0-9]*/stat; do read line < $f; case \"$line\" in *'(4vrs-web)'*|*'(4vrs-gateway)'*|*'(4vrs-kdf)'*|*'(4vrs-rng)'*) echo \"$line\";; esac; done")
                emit(log,'resources',phase=phase['name'],load_active=phase['load_active'],exit=p.returncode,stat=p.stdout.decode(errors='replace')[:8192])
            except (OSError,subprocess.TimeoutExpired) as e:emit(log,'collector_error',phase=phase['name'],error=type(e).__name__)
            stop.wait(c['sample_period_s'])
    emit(log,'begin',protocol=c['protocol'],authorization=c['authorization_reference'],modbus=bool(c.get('modbus')),scope='Read-only; SSH collector overhead included; no device mode changes')
    collector=threading.Thread(target=collect,daemon=True);collector.start();sequence=0
    try:
        for plan in c['phases']:
            phase.update(name=plan['name'],load_active=False);emit(log,'phase',name=plan['name']);end=time.monotonic()+plan['seconds'];modstop=threading.Event();modthread=None
            if c.get('modbus'):
                spec=importlib.util.spec_from_file_location('optional_modbus',Path(__file__).with_name('web-autonomous-harness.py'));h=importlib.util.module_from_spec(spec);spec.loader.exec_module(h);h.emit=emit
                modthread=threading.Thread(target=h.modbus,args=(c,modstop,log,plan['name']),daemon=True);modthread.start()
            try:
                if plan['name']!='mixed':stop.wait(max(0,end-time.monotonic()));continue
                # Same phases for HTTP and HTTPS; parallel refusal is recorded, not hidden.
                for scenario in ('cold','reuse','parallel'):
                    if time.monotonic()>=end:break
                    phase['load_active']=True;emit(log,'load_started',phase='mixed',scenario=scenario)
                    if scenario=='parallel':
                        barrier=threading.Barrier(4);threads=[]
                        for i in range(4):
                            sequence+=1;t=threading.Thread(target=measure,args=(c,log,sequence,['/','/api/hello'],end,scenario,barrier),daemon=True);threads.append(t);t.start()
                        for t in threads:t.join(max(0,end-time.monotonic()+1))
                        if any(t.is_alive() for t in threads):emit(log,'abort',reason='worker deadline');return
                    elif scenario=='reuse':
                        sequence+=1;measure(c,log,sequence,['/','/app.js','/style.css','/api/hello'],end,scenario)
                    else:
                        for path in ['/','/app.js','/style.css','/api/hello']:
                            sequence+=1;measure(c,log,sequence,[path],end,scenario);stop.wait(.1)
                    phase['load_active']=False;emit(log,'load_stopped',phase='mixed',scenario=scenario)
                    stop.wait(min(3,max(0,end-time.monotonic())))
                phase['load_active']=False;emit(log,'synthetic_complete',phase='mixed');stop.wait(max(0,end-time.monotonic()))
            finally:
                phase['load_active']=False;modstop.set()
                if modthread:modthread.join(11)
    finally:
        stop.set();collector.join(11);emit(log,'end')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--profile',type=Path);p.add_argument('--log',type=Path,required=True);p.add_argument('--validate-only',action='store_true');p.add_argument('--mark',choices=['vpn-change','login','navigation','backlight-off','backlight-on','network-scenario-separately-approved']);a=p.parse_args()
    if a.mark:emit(a.log,'operator_action',action=a.mark)
    else:
        c=json.loads(a.profile.read_text(encoding='utf8'));validate(c)
        if a.validate_only:print('Profile valid; no connection made')
        elif a.log.exists():raise SystemExit('Use a new log path')
        else:run(c,a.log)
