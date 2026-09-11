"""Embed only reviewed local routes; emit an integrity manifest beside C output."""
import hashlib,json,sys,base64,re
from pathlib import Path
root,assets,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
items=[('/',root/'src/web/ui/index.html','text/html; charset=utf-8'),('/app.js',root/'src/web/ui/app.js','text/javascript; charset=utf-8'),('/style.css',root/'src/web/ui/style.css','text/css; charset=utf-8'),('/favicon.ico',assets/'favicon.ico','image/x-icon'),('/apple-touch-icon.png',assets/'favicon-180.png','image/png')]
items.append(('/extended.js',root/'src/web/ui/extended.js','text/javascript; charset=utf-8'))
for lang in ('en','ru'):items.append(('/help/'+lang+'.html',root/('src/web/ui/help-'+lang+'.html'),'text/html; charset=utf-8'))
for name in ('main-menu','configuration'):items.append(('/help/'+name+'.png',assets.parent/'help'/(name+'.png'),'image/png'))
# HTML tokenization normalizes CRLF/CR to LF before applying inline CSP hashes.
# Hash the exact normalized text that the browser executes, on Windows too.
script=(root/'src/web/ui/app.js').read_text(encoding='utf8').encode()+b'\n'+(root/'src/web/ui/extended.js').read_text(encoding='utf8').encode()
style=(root/'src/web/ui/style.css').read_text(encoding='utf8').encode()
assert b'</script' not in script.lower() and b'</style' not in style.lower()
def tag(b):return base64.b64encode(hashlib.sha256(b).digest()).decode()
csp="default-src 'self'; script-src 'self' 'sha256-"+tag(script)+"'; style-src 'self' 'sha256-"+tag(style)+"'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'"
lines=['#include "web/web_assets.h"','const char web_inline_csp[]='+json.dumps(csp)+';'];records=[]
for i,(route,path,mime) in enumerate(items):
 data=path.read_bytes()
 if route=='/':
  html=data.decode('utf8');html=re.sub(r'<link[^>]+>|<script[^>]+></script>','',html)
  icon='data:image/x-icon;base64,'+base64.b64encode((assets/'favicon.ico').read_bytes()).decode()
  html=html.replace('</head>','<link rel="icon" href="'+icon+'"><style>'+style.decode('utf8')+'</style></head>')
  html=html.replace('</body>','<script>'+script.decode('utf8')+'</script></body>');data=html.encode('utf8')
 if route.startswith('/help/') and route.endswith('.html'):
  for name in ('main-menu','configuration'):
   data=data.replace(('/help/'+name+'.png').encode(),b'data:image/png;base64,'+base64.b64encode((assets.parent/'help'/(name+'.png')).read_bytes()))
 assert len(data)<=3*1024*1024
 records.append({'route':route,'source':str(path),'size':len(data),'sha256':hashlib.sha256(data).hexdigest()})
 lines.append('static const unsigned char asset%d[]={%s};'%(i,','.join(map(str,data))))
lines.append('const web_asset_t web_assets[]={'+','.join('{"%s","%s",asset%d,sizeof(asset%d),"%s"}'%(route,mime,i,i,records[i]['sha256']) for i,(route,path,mime) in enumerate(items))+',{0,0,0,0,0}};')
(out/'web-assets.c').write_text('\n'.join(lines)+'\n');(out/'web-assets.json').write_text(json.dumps(records,indent=2)+'\n')
