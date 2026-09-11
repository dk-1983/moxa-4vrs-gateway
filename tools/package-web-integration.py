"""Development bundle only. No native installer, startup mutation or secrets."""
import hashlib,io,json,sys,tarfile
from pathlib import Path
root,build,tls,output=map(Path,sys.argv[1:]);items={}
for name in ('4vrs-gateway','4vrs-web'):items['bin/'+name]=(build/name).read_bytes()
for name in ('index.html','app.js','style.css'):items['ui/'+name]=(root/'src/web/ui'/name).read_bytes()
for name in ('favicon.ico','favicon-180.png'):items['ui/'+name]=(root/'assets/icons'/name).read_bytes()
items['licenses/Mbed-TLS-LICENSE']=(tls/'LICENSE').read_bytes()
items['NOTICE.txt']=b'4VRS Gateway Web integration development build v2026.02.01.\nMbed TLS 3.6.7, Copyright The Mbed TLS Contributors. Apache-2.0 option selected.\nTLS library source is unmodified; project-specific configuration is src/web/mbedtls_config.h.\n'
items['NOT-INSTALLABLE.txt']=b'DEVELOPMENT ARTIFACT ONLY. Native installer format 2 and crash recovery are not integrated.\nDo not install this archive on a device. No installer, startup files, administrator verifier or private key is included.\nUI resources are also embedded in bin/4vrs-web.\n'
manifest={k:{'bytes':len(v),'sha256':hashlib.sha256(v).hexdigest()} for k,v in items.items()};items['manifest.json']=(json.dumps(manifest,sort_keys=True,indent=2)+'\n').encode()
output.parent.mkdir(parents=True,exist_ok=True)
with tarfile.open(output,'w:gz') as tar:
 for name,data in sorted(items.items()):
  info=tarfile.TarInfo(name);info.size=len(data);info.mode=0o755 if name.startswith('bin/') else 0o644;info.mtime=0;tar.addfile(info,io.BytesIO(data))
with tarfile.open(output) as tar:
 for name,record in manifest.items():assert hashlib.sha256(tar.extractfile(name).read()).hexdigest()==record['sha256']
print(json.dumps({'artifact':str(output),'bytes':output.stat().st_size,'sha256':hashlib.sha256(output.read_bytes()).hexdigest(),'installable':False}))
