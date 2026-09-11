"""Loopback UI-only fixture. No device, crypto or real authentication."""
from http.server import ThreadingHTTPServer,BaseHTTPRequestHandler
from pathlib import Path
import json
root=Path(__file__).resolve().parents[2]/'src/web/ui'
class Handler(BaseHTTPRequestHandler):
 def log_message(self,*args):pass
 def do_POST(self):
  self.rfile.read(min(4096,int(self.headers.get('Content-Length',0))))
  b=b'{"revision":"fixture","csrf":"fixture","data":{}}'
  self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
 def do_GET(self):
  path=self.path.split('?')[0]
  if path.startswith('/api/'):
   op=path[5:];mode=self.headers.get('Referer','')
   data={'configured':'enroll' not in mode,'enrollment':True} if op=='hello' else {'time':[2026,9,10,10,0,0],'web_enabled':1,'web_interface':0,'backlight':1,'ntp_enabled':0,'ntp_server':'','ntp_interval':1}
   status=401 if op=='overview' and 'system' not in mode else 200
   b=json.dumps({'revision':'fixture','csrf':'fixture' if 'system' in mode else '', 'data':data}).encode();kind='application/json'
  else:
   file=root/('index.html' if path=='/' else path.lstrip('/'))
   if file.parent!=root or not file.is_file():self.send_error(404);return
   b=file.read_bytes();kind='text/javascript' if file.suffix=='.js' else 'text/css' if file.suffix=='.css' else 'text/html';status=200
  self.send_response(status);self.send_header('Content-Type',kind+'; charset=utf-8');self.send_header('Content-Length',str(len(b)));self.end_headers();self.wfile.write(b)
ThreadingHTTPServer(('127.0.0.1',18888),Handler).serve_forever()
