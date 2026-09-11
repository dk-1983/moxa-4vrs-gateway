"""Real exec worker compared with independent hashlib at unchanged 600000 cost."""
from pathlib import Path
import hashlib,os,socket,struct,sys,time
binary=Path(sys.argv[1]).resolve()
for length in [12,24,63,64,65,128]:
    password=bytes((i*37+length)%256 for i in range(length));salt=bytes(range(16))
    msg=bytearray(328);msg[:4]=b'KDF1';msg[5]=length;msg[8:8+length]=password;msg[264:280]=salt
    a,b=socket.socketpair();os.dup2(a.fileno(),70000,inheritable=True);pid=os.fork()
    if not pid:
        x=os.dup(b.fileno());os.dup2(x,3);os.closerange(4,1024);os.close(0);os.close(1);os.execv(binary,[str(binary)])
    os.close(70000);b.close();a.settimeout(60)
    # Child blocks waiting for complete input; inspect exec/fd isolation first.
    end=time.monotonic()+3
    while time.monotonic()<end:
        if Path('/proc/'+str(pid)+'/exe').resolve()==binary and {p.name for p in Path('/proc/'+str(pid)+'/fd').iterdir()} <= {'2','3'}:break
        time.sleep(.001)
    assert Path('/proc/'+str(pid)+'/exe').resolve()==binary
    assert {p.name for p in Path('/proc/'+str(pid)+'/fd').iterdir()} <= {'2','3'}
    assert password not in Path('/proc/'+str(pid)+'/cmdline').read_bytes()
    assert password not in Path('/proc/'+str(pid)+'/environ').read_bytes()
    a.sendall(msg[:100]);a.sendall(msg[100:]);a.shutdown(socket.SHUT_WR)
    data=b''
    while len(data)<32:
        z=a.recv(32-len(data))
        if not z:break
        data+=z
    a.close();_,status=os.waitpid(pid,0);assert os.waitstatus_to_exitcode(status)==0
    assert data==hashlib.pbkdf2_hmac('sha256',password,salt,600000)
print('KDF exec: 6 independent full-cost vectors, 12..128 bytes/pad boundaries/binary inputs; fd and argv isolation PASS')
