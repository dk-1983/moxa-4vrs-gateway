"""Host OpenSSL client versus the exact Mbed TLS target feasibility profile."""
import json
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path
binary, out = map(Path, sys.argv[1:])
out.mkdir(parents=True,exist_ok=True)
cert=out/'cert.pem'
for failure in ('--unsynced','--entropy-fail'):
    result=subprocess.run([str(binary),failure,str(out/'forbidden.pem')],capture_output=True)
    assert result.returncode and not (out/'forbidden.pem').exists(),failure
request=b'GET /api/v1/status HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n'
for scenario in ('valid','slow','oversized','command','bad-host'):
    process=subprocess.Popen([str(binary),'--serve',str(cert)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    try:
        assert process.stdout.readline().startswith('READY'),process.communicate()
        start=time.monotonic()
        raw=socket.create_connection(('127.0.0.1',18443),timeout=5)
        if scenario=='slow':
            assert process.wait(timeout=5)!=0
            assert time.monotonic()-start<4.5
            raw.close()
            continue
        context=ssl.create_default_context(cafile=str(cert))
        context.maximum_version=ssl.TLSVersion.TLSv1_2
        with context.wrap_socket(raw,server_hostname='127.0.0.1') as client:
            assert client.version()=='TLSv1.2'
            payload=request if scenario=='valid' else b'A'*2049 if scenario=='oversized' else request.replace(b'GET',b'POST') if scenario=='command' else request.replace(b'127.0.0.1',b'evil.test')
            client.sendall(payload)
            response=b''
            try:
                while True:
                    part=client.recv(4096)
                    if not part:break
                    response+=part
            except (ConnectionResetError,ssl.SSLError):pass
        code=process.wait(timeout=5)
        if scenario=='valid':
            assert code==0,(code,process.stderr.read())
            header,body=response.split(b'\r\n\r\n',1)
            assert b'200 OK' in header and b'Content-Length: '+str(len(body)).encode() in header
            assert json.loads(body)=={'foundation':'local'}
        else:assert code!=0 and not response
    finally:
        if process.poll() is None:process.kill()
        process.communicate()
print('TLS integration: trusted test cert/IP SAN/TLS1.2/status/slow/oversize/commands/Host/entropy/clock PASS')
