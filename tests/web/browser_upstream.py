"""Docker-side loopback-only bridge for Windows browser validation."""
import base64,http.client,json,ssl,sys
r=json.load(sys.stdin)
c=http.client.HTTPSConnection('127.0.0.1',18443,context=ssl._create_unverified_context(),timeout=65)
c.request(r['method'],r['path'],base64.b64decode(r['body']),r['headers'])
s=c.getresponse();print(json.dumps({'status':s.status,'headers':s.getheaders(),'body':base64.b64encode(s.read()).decode()}));c.close()
