"""Socket-pair Modbus framing/deadline and incomplete profile refusal; no network."""
import importlib.util,json,socket,struct,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
s=importlib.util.spec_from_file_location('harness',root/'tools/web-autonomous-harness.py');h=importlib.util.module_from_spec(s);s.loader.exec_module(h)
try:h.validate(json.loads((root/'tools/web-autonomous-profile.example.json').read_text()))
except ValueError:pass
else:raise AssertionError('Unapproved example accepted')
a,b=socket.socketpair()
try:
 b.sendall(b'ab');b.sendall(b'cd');assert h.exact(a,4,time.monotonic()+1)==b'abcd'
 begin=time.monotonic()
 try:h.exact(a,1,begin+.05)
 except TimeoutError:assert time.monotonic()-begin<.5
 else:raise AssertionError('Deadline not enforced')
 b.close()
 try:h.exact(a,1,time.monotonic()+1)
 except ConnectionError:pass
 else:raise AssertionError('Disconnect not detected')
finally:a.close();b.close()
original=h.socket.create_connection
for bad in [False,True]:
 a,b=socket.socketpair();stop=threading.Event();events=[]
 def server():
  wire=h.exact(b,12,time.monotonic()+1);tid,proto,length,unit,fn,address,count=struct.unpack('>HHHBBHH',wire)
  assert (proto,length,unit,fn,address,count)==(0,6,7,3,123,2)
  b.sendall(struct.pack('>HHHBB',tid+(1 if bad else 0),0,7,unit,fn)+b'\x04\x00\x01\x00\x02');b.close()
 def record(path,event,**data):events.append(data);stop.set()
 h.socket.create_connection=lambda *args,**kw:a;h.emit=record
 t=threading.Thread(target=server);t.start()
 h.modbus({'target':'fixture-only','modbus':{'port':1,'unit':7,'function':3,'address':123,'count':2,'period_s':1,'timeout_s':1}},stop,None,'baseline');t.join()
 assert len(events)==1 and bool(events[0]['error'])==bad and set(events[0])=={'phase','seconds','error'}
h.socket.create_connection=original
print('Autonomous harness: incomplete profile refused before SSH; fragmented read/deadline/disconnect PASS')
