"""Wrap two validated platform archives; no device access and no recovery logic."""
import argparse
import gzip
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tarfile

spec=importlib.util.spec_from_file_location('package',Path(__file__).with_name('build-installer-package.py'))
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)
PAYLOAD={'4vrs-install','4vrs-gateway','4vrs-web','4vrs-rng','4vrs-kdf','4vrs-gateway.init','4vrs-networking-wrapper'}
NAMES=PAYLOAD|{'manifest.json','SHA256SUMS','LICENSE.mbedtls','NOTICE'}

def load(path,platform,version):
    raw=package.read_regular(path,32*1024*1024);files={}
    with tarfile.open(fileobj=io.BytesIO(raw),mode='r:gz') as archive:
        for member in archive:
            name=member.name.rsplit('/',1)[-1]
            if (not member.isfile() or member.name!='4vrs-gateway-'+version+'/'+name
                    or name not in NAMES or name in files or not 0<member.size<=package.MAX_BINARY):
                raise ValueError('unexpected archive member')
            files[name]=archive.extractfile(member).read()
    if set(files)!=NAMES:raise ValueError('incomplete platform archive')
    manifest=json.loads(files['manifest.json'])
    if (manifest['version']!=version or manifest['format']!=3 or manifest['product']!='4VRS Gateway'
            or manifest['entrypoint']!='4vrs-install' or len(manifest['files'])!=len(PAYLOAD)
            or {x['name'] for x in manifest['files']}!=PAYLOAD):
        raise ValueError('incompatible manifest')
    for item in manifest['files']:
        name=item['name'];data=files[name]
        if item['size']!=len(data) or item['mode']!='0755' or item['sha256']!=hashlib.sha256(data).hexdigest():
            raise ValueError('payload digest or metadata mismatch')
        if name in ('4vrs-gateway.init','4vrs-networking-wrapper'):package.check_script(data)
        else:package.check_target(data,version,platform)
    expected=PAYLOAD|{'manifest.json'};seen=set()
    for line in files['SHA256SUMS'].decode('ascii').splitlines():
        digest,name=line.split('  ')
        if name not in expected or name in seen or digest!=hashlib.sha256(files[name]).hexdigest():
            raise ValueError('invalid checksum list')
        seen.add(name)
    if seen!=expected:raise ValueError('incomplete checksum list')
    return files

def build(version,linux24,linux26,launcher,output):
    # Validate both halves before creating any output, even if only one is needed today.
    contents={}
    for platform,path in [('linux24',linux24),('linux26',linux26)]:
        for name,data in load(path,platform,version).items():
            contents['platforms/'+platform+'/'+name]=(data,0o755 if name in PAYLOAD else 0o644)
    script=package.read_regular(launcher,package.MAX_SCRIPT);package.check_script(script)
    contents['install.sh']=(script,0o755)
    contents['README.txt']=(('4VRS Gateway '+version+' universal candidate\nExtract on the Moxa CF, enter this directory and run: sh install.sh\nRead-only platform check: sh install.sh --detect\nNo manual OS selection. Unsupported platforms are refused.\nThe selected installer performs validation and transactional installation.\nThis bundle does not itself establish hardware qualification.\n').encode(),0o644)
    contents['SHA256SUMS']=(''.join(hashlib.sha256(data).hexdigest()+'  '+name+'\n' for name,(data,_) in sorted(contents.items())).encode(),0o644)
    buffer=io.BytesIO()
    with tarfile.open(fileobj=buffer,mode='w',format=tarfile.USTAR_FORMAT) as archive:
        for name,(data,mode) in sorted(contents.items()):
            item=tarfile.TarInfo('4vrs-gateway-'+version+'-universal/'+name);item.mode=mode;item.size=len(data)
            item.uid=item.gid=item.mtime=0;archive.addfile(item,io.BytesIO(data))
    compressed=gzip.compress(buffer.getvalue(),mtime=0)
    with Path(output).open('xb') as stream:stream.write(compressed)
    return hashlib.sha256(compressed).hexdigest()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('version','linux24','linux26','launcher','output'):parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    print(build(**vars(args)))
