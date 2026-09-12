"""Build a pinned, reproducible portable CF kit without reading device state."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import zipfile

def build(source, archive, worker, output):
    source=source.resolve()
    spec=importlib.util.spec_from_file_location('kit_package',source/'tools/cf-package.py')
    package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
    payload=package.load(archive)
    files={name:(source/'tools'/name).read_bytes() for name in ('cf-wizard.py','cf-bootstrap.py','cf-package.py')}
    worker_bytes=worker.read_bytes()
    if worker_bytes[:6]!=b'\x7fELF\x02\x01' or b'CF_BOOTSTRAP_FIXTURE_IMAGE_ONLY' in worker_bytes:
        raise ValueError('expected production Linux x86-64 worker')
    files.update({'gateway.tar.gz':archive.read_bytes(),'4vrs-cf-rng-worker':worker_bytes,
                  'LICENSE':(source/'LICENSE').read_bytes(),
                  'LICENSE.mbedtls':payload['platforms/linux24/LICENSE.mbedtls'][0]})
    queue=['docs/cf-wizard.ru.md','docs/cf-wizard.md']
    while queue:
        name=queue.pop()
        if name in files:continue
        path=(source/name).resolve()
        relative=path.relative_to(source).as_posix()
        if not (relative.startswith(('docs/','assets/')) or relative=='LICENSE' or ('/' not in relative and relative.endswith('.md'))):
            raise ValueError('unsupported documentation reference: '+relative)
        data=path.read_bytes();files[relative]=data
        if path.suffix=='.md':
            text=re.sub(r'```.*?```','',data.decode('utf-8'),flags=re.S)
            for link in re.findall(r'\]\(([^)]+)\)',text)+re.findall(r'(?:src|href)="([^"]+)"',text):
                link=link.split('#',1)[0]
                if not link or re.match(r'^[a-zA-Z]+:',link):continue
                target=(path.parent/link).resolve()
                if not target.is_file():
                    # Published build artifacts are delivery links, not kit dependencies.
                    if '/build/' in target.as_posix():continue
                    raise ValueError('missing documentation reference: '+str(target))
                queue.append(target.relative_to(source).as_posix())
    files['README.ru.md']='# Мастер CF v2026.02.03\n\n[Порядок подготовки и установки](docs/cf-wizard.ru.md).\n'.encode()
    files['README.md']=b'# CF wizard v2026.02.03\n\n[Preparation and installation](docs/cf-wizard.md).\n'
    sha=lambda b:hashlib.sha256(b).hexdigest()
    records=lambda data:{n:{'bytes':len(b),'sha256':sha(b)} for n,b in sorted(data.items())}
    files['manifest.json']=(json.dumps({'format':1,'generic_target':True,'contains_seed':False,'contains_fixture_worker':False,'files':records(files)},indent=2)+'\n').encode()
    files['SHA256SUMS']=''.join(sha(b)+'  '+n+'\n' for n,b in sorted(files.items())).encode()
    files['MANIFEST.json']=(json.dumps(records(files),indent=2)+'\n').encode()
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED) as z:
        for name,data in sorted(files.items()):
            info=zipfile.ZipInfo(name,(2026,9,12,0,0,0));info.create_system=3
            info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=(0o100755 if name=='4vrs-cf-rng-worker' else 0o100644)<<16
            z.writestr(info,data)
    print(json.dumps({'sha256':sha(output.read_bytes()),'bytes':output.stat().st_size,'files':len(files)}))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True)
    p.add_argument('--archive',type=Path,required=True);p.add_argument('--worker',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    build(a.source,a.archive,a.worker,a.output)
