"""Explicit disposable host fixtures. Never target the device or export state."""
from pathlib import Path
import os,socket,struct,subprocess,sys,tempfile,time,signal,json,array
binary=Path(sys.argv[1]).resolve();checks=[]
def check(name,v):
 assert v,name
 checks.append(name);print('PASS',name,flush=True)
def helper(d,op='status',data=None,env=None):
 return subprocess.run([binary,op,d],input=data,capture_output=True,timeout=35,env=env)
def launch(d):
 a,x=socket.socketpair();b,y=socket.socketpair();pid=os.fork()
 if not pid:
  xx=os.dup(x.fileno());yy=os.dup(y.fileno())
  os.dup2(x.fileno(),70000,inheritable=True)
  os.dup2(xx,3);os.dup2(yy,4)
  os.closerange(5,1024)
  os.execv(binary,[str(binary),'serve',str(d)])
 x.close();y.close();a.settimeout(5);b.settimeout(5);return pid,a,b
def reap(pid):
 end=time.monotonic()+5
 while time.monotonic()<end:
  p,s=os.waitpid(pid,os.WNOHANG)
  if p:return os.waitstatus_to_exitcode(s)
  time.sleep(.01)
 os.kill(pid,signal.SIGKILL);os.waitpid(pid,0);raise AssertionError('broker hung')
def packet(s,seq,n,fragment=False):
 h=b'RNG1'+struct.pack('!III',seq,n,0)
 if fragment:
  for x in h:s.sendall(bytes([x]))
 else:s.sendall(h)
 out=b''
 while len(out)<16+n:
  z=s.recv(16+n-len(out))
  if not z:break
  out+=z
 return out
with tempfile.TemporaryDirectory(prefix='product-nv-',dir=binary.parent) as tmp:
 d=Path(tmp);check('missing status',helper(d).returncode==3)
 for n in [0,31,33]:check('transport length '+str(n),helper(d,'provision',bytes(n)).returncode!=0 and not (d/'state').exists())
 r=helper(d,'provision',bytes(range(32)));check('provision',r.returncode==0)
 check('payload absent from logs',bytes(range(32)) not in r.stdout+r.stderr)
 check('lost reply status is policy-required',helper(d).returncode==5)
 before=(d/'state').read_bytes();check('no blind reprovision',helper(d,'provision',bytes(32)).returncode!=0 and (d/'state').read_bytes()==before)
 (d/'trial-policy').write_bytes(b'trial-v1\n');(d/'trial-policy').chmod(0o600)
 pid,a,b=launch(d)
 try:
  v=packet(a,1,1024,True);check('fragmented full response',len(v)==1040 and v[:12]==b'RNG1'+struct.pack('!II',1,1024))
  check('broker exec rejects extra high fd',Path('/proc/'+str(pid)+'/exe').resolve()==binary and not Path('/proc/'+str(pid)+'/fd/70000').exists())
  epoch=struct.unpack('!I',v[12:16])[0];check('committed epoch',epoch==2)
  w=packet(b,1,32);check('independent private channel',len(w)==48 and w[12:16]==v[12:16])
  check('status does not wait for owner lock',helper(d).returncode==0)
  new,passed=socket.socketpair();new.settimeout(5)
  a.sendmsg([b'RGA1'+struct.pack('!III',2,0,0)],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[passed.fileno()]))]);passed.close()
  ack=a.recv(16);check('replace Web capability ack',len(ack)==16 and ack[:12]==b'RNG1'+struct.pack('!II',2,0))
  check('old Web channel revoked',b.recv(16)==b'')
  check('new Web independent sequence same broker epoch',packet(new,1,16)[12:16]==v[12:16]);new.close()
  # Closing the new Web channel terminates broker; here retain old test's
  # framing check on a new owner below instead of racing EOF with invalid data.
  check('Web channel disconnect terminates owner',reap(pid)==22);pid=0
 finally:
  a.close();b.close()
  if pid:os.kill(pid,signal.SIGTERM);reap(pid)
 pid,a,b=launch(d)
 try:
  v=packet(a,1,32);check('restart new epoch',struct.unpack('!I',v[12:16])[0]==3)
  a.sendall(b'R');time.sleep(4.2);check('partial frame deadline',reap(pid)==21);pid=0
 finally:
  a.close();b.close()
  if pid:os.kill(pid,signal.SIGTERM);reap(pid)
 # All persistence errors fail before a response; host injection only.
 for boundary in ['read','created','before-write','written','before-file-fsync','file-fsync','before-rename','renamed','before-directory-fsync','directory-fsync']:
  case=d/boundary;case.mkdir(mode=0o700);check('fixture provision '+boundary,helper(case,'provision',bytes(range(32))).returncode==0)
  (case/'trial-policy').write_bytes(b'trial-v1\n');(case/'trial-policy').chmod(0o600)
  os.environ['NV_FAIL']=boundary
  pid,a,b=launch(case);del os.environ['NV_FAIL']
  try:
   try:response=packet(a,1,32)
   except (BrokenPipeError,ConnectionResetError):response=b''
   check('no output on '+boundary,not response and reap(pid)==4);pid=0
  finally:
   a.close();b.close()
   if pid:os.kill(pid,signal.SIGTERM);reap(pid)
 # Process crash at each durable-write boundary, distinct from returned I/O
 # errors. This proves process recovery only, not physical CF power-loss.
 for boundary in ['created','before-write','written','before-file-fsync','file-fsync','before-rename','renamed','before-directory-fsync','directory-fsync']:
  case=d/('crash-'+boundary);case.mkdir(mode=0o700);assert helper(case,'provision',bytes(range(32))).returncode==0
  (case/'trial-policy').write_bytes(b'trial-v1\n');(case/'trial-policy').chmod(0o600)
  os.environ['NV_CRASH']=boundary
  pid,a,b=launch(case);del os.environ['NV_CRASH']
  try:
   try:response=packet(a,1,32)
   except (BrokenPipeError,ConnectionResetError):response=b''
   check('crash before output '+boundary,not response and reap(pid)==77);pid=0
  finally:
   a.close();b.close()
   if pid:os.kill(pid,signal.SIGTERM);reap(pid)
  pid,a,b=launch(case)
  try:check('crash recovery '+boundary,len(packet(a,1,32))==48 and not (case/'pending').exists())
  finally:a.close();b.close();reap(pid)
 for label,frame in [('oversize',b'RNG1'+struct.pack('!III',1,1025,0)),('zero',b'RNG1'+struct.pack('!III',1,0,0)),('sequence',b'RNG1'+struct.pack('!III',2,32,0)),('epoch',b'RNG1'+struct.pack('!III',1,32,1))]:
  pid,a,b=launch(d)
  try:
   a.sendall(frame)
   try:response=a.recv(1040)
   except ConnectionResetError:response=b''
   check('invalid framing '+label,not response and reap(pid)==23);pid=0
  finally:
   a.close();b.close()
   if pid:os.kill(pid,signal.SIGTERM);reap(pid)
 # Binary stdin transport with explicit EOF/disconnect. No fixture payload
 # is persisted outside this disposable directory or written to logs.
 for size in (12,32):
  case=d/('transport-'+str(size));case.mkdir(mode=0o700)
  send,end=socket.socketpair()
  proc=subprocess.Popen([binary,'provision',case],stdin=end,stdout=subprocess.PIPE,stderr=subprocess.PIPE);end.close()
  send.sendall(bytes(range(size)));send.shutdown(socket.SHUT_WR);send.close()
  output,errors=proc.communicate(timeout=35)
  check('transport disconnect '+str(size),(proc.returncode==0)==(size==32) and bytes(range(size)) not in output+errors)
  if size==32:check('lost transport ack resolved by status',helper(case).returncode==5 and helper(case,'provision',bytes(32)).returncode!=0)
 # One application response buffered per channel; more than eight queued
 # headers is rejected, rather than unbounded allocation/work.
 pid,a,b=launch(d)
 try:
  os.kill(pid,signal.SIGSTOP);time.sleep(.02)
  a.sendall(b''.join(b'RNG1'+struct.pack('!III',i,32,0) for i in range(1,10)))
  os.kill(pid,signal.SIGCONT);check('bounded queue rejects saturation',reap(pid)==26);pid=0
 finally:
  a.close();b.close()
  if pid:os.kill(pid,signal.SIGTERM);reap(pid)
 os.environ['NV_FAST_ROTATION']='1'
 pid,a,b=launch(d);del os.environ['NV_FAST_ROTATION']
 try:
  epoch=None
  for i in range(1,1026):
   v=packet(a,i,1)
   if epoch is None:epoch=v[12:16]
   assert len(v)==17 and v[12:16]==epoch
  check('rotation commits before output with stable broker epoch',int.from_bytes((d/'state').read_bytes()[8:12],'big')==int.from_bytes(epoch,'big')+1)
 finally:
  a.close();b.close();reap(pid)
 os.environ['NV_FAST_ROTATION']='1';os.environ['NV_ROTATE_FAIL']='before-directory-fsync'
 pid,a,b=launch(d);del os.environ['NV_FAST_ROTATION'];del os.environ['NV_ROTATE_FAIL']
 try:
  for i in range(1,1025):assert len(packet(a,i,1))==17
  try:response=packet(a,1025,1)
  except (BrokenPipeError,ConnectionResetError):response=b''
  check('rotation failure emits no response',not response and reap(pid)==25);pid=0
 finally:
  a.close();b.close()
  if pid:os.kill(pid,signal.SIGTERM);reap(pid)
print(json.dumps({'contracts':checks,'production_seed':False},indent=2))
