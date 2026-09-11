"""Loopback-only raw relay for Chrome QA of real Web in network-none Docker.
No content logging, hardware addresses, or product installation. Host QA only.
"""
import os,socket,subprocess,sys,threading,time
from pathlib import Path
if sys.argv[1]=='bridge':
    port=int(sys.argv[2]);assert port in (20080,20443)
    s=socket.create_connection(('127.0.0.1',port),timeout=10);s.settimeout(90)
    def incoming():
        try:
            while True:
                b=os.read(0,16384)
                if not b:break
                s.sendall(b)
        except OSError:pass
        finally:
            try:s.shutdown(socket.SHUT_WR)
            except OSError:pass
    threading.Thread(target=incoming,daemon=True).start()
    try:
        while True:
            b=s.recv(16384)
            if not b:break
            sys.stdout.buffer.write(b);sys.stdout.buffer.flush()
    finally:s.close()
else:
    assert sys.argv[1]=='host';stop=Path(sys.argv[2]);until=time.monotonic()+1800;slots=threading.BoundedSemaphore(8)
    def client(c,port):
        p=None
        try:
            p=subprocess.Popen(['docker','exec','-i','moxa-dev-20260909-064851','python3','/workspace/source-host/tests/web/browser_relay.py','bridge',str(port)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
            c.settimeout(90)
            def incoming():
                try:
                    while True:
                        b=c.recv(16384)
                        if not b:break
                        p.stdin.write(b);p.stdin.flush()
                except (OSError,ValueError):pass
                finally:
                    try:p.stdin.close()
                    except OSError:pass
            threading.Thread(target=incoming,daemon=True).start()
            while True:
                b=os.read(p.stdout.fileno(),16384)
                if not b:break
                c.sendall(b)
        except OSError:pass
        finally:
            c.close()
            if p and p.poll() is None:p.terminate()
            slots.release()
    def listen(port):
        with socket.socket() as s:
            s.bind(('127.0.0.1',port));s.listen(4);s.settimeout(.3)
            while time.monotonic()<until and not stop.exists():
                try:c,_=s.accept()
                except socket.timeout:continue
                if not slots.acquire(blocking=False):c.close();continue
                threading.Thread(target=client,args=(c,port),daemon=True).start()
    threads=[threading.Thread(target=listen,args=(p,)) for p in (20080,20443)]
    for t in threads:t.start()
    for t in threads:t.join()
