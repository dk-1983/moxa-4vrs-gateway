#!/usr/bin/env python3
import socket,struct,sys
host=sys.argv[1];port=int(sys.argv[2]);count=int(sys.argv[3]) if len(sys.argv)>3 else 5
unit=int(sys.argv[4]) if len(sys.argv)>4 else 8
def recv_exact(s,n):
 b=b''
 while len(b)<n:
  x=s.recv(n-len(b))
  if not x: raise RuntimeError('closed')
  b+=x
 return b
with socket.create_connection((host,port),timeout=5) as s:
 s.settimeout(5)
 for n in range(count):
  tid=0x1001+n;req=struct.pack('>HHHBBHH',tid,0,6,unit,4,0,40)
  print('MBAP_TX='+req.hex().upper());s.sendall(req)
  h=recv_exact(s,7);length=struct.unpack('>H',h[4:6])[0];body=recv_exact(s,length-1);resp=h+body
  print('MBAP_RX='+resp.hex().upper())
  assert struct.unpack('>H',h[:2])[0]==tid and h[6]==unit and body[0]==4 and body[1]==80
  if n==0:
   regs=struct.unpack('>40H',body[2:])
   for ch in range(8):
    p=ch*5;fv=struct.unpack('>f',struct.pack('>HH',regs[p+3],regs[p+4]))[0]
    iv=regs[p+1] if regs[p+1]<32768 else regs[p+1]-65536
    print(f'CH{ch+1} decimal={regs[p]} int={iv} error={regs[p+2]} float={fv!r}')
print('POLLS_OK='+str(count))
