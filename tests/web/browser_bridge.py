"""Local TLS bridge into the network-isolated production test server.
Uses only generated host-test keys. Binds Windows loopback, never hardware.
"""
import base64,http.server,json,ssl,subprocess,sys
from pathlib import Path
container,security=sys.argv[1:];security=Path(security)
bundle=(security/'tls.current').read_bytes();key=bundle[bundle.index(b'-----BEGIN'):bundle.index(b'-----BEGIN CERTIFICATE')];cert=bundle[bundle.index(b'-----BEGIN CERTIFICATE'):]
(security/'browser-key.pem').write_bytes(key);(security/'browser-cert.pem').write_bytes(cert)
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):self.forward()
 def do_POST(self):self.forward()
 def forward(self):
  n=int(self.headers.get('Content-Length',0))
  if n>4096:self.send_error(413);return
  payload={'method':self.command,'path':self.path,'body':base64.b64encode(self.rfile.read(n)).decode(),'headers':dict(self.headers)}
  p=subprocess.run(['docker','exec','-i',container,'python3','/workspace/source-host/tests/web/browser_upstream.py'],input=json.dumps(payload).encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=70)
  if p.returncode:self.send_error(502);return
  r=json.loads(p.stdout);self.send_response(r['status'])
  for k,v in r['headers']:self.send_header(k,v)
  self.end_headers();self.wfile.write(base64.b64decode(r['body']))
 def log_message(self,*args):pass
server=http.server.HTTPServer(('127.0.0.1',18443),Handler);ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(security/'browser-cert.pem',security/'browser-key.pem');server.socket=ctx.wrap_socket(server.socket,server_side=True);server.serve_forever()
