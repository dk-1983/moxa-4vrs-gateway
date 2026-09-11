"""Immutable trial8 archive -> private Linux root -> production cut/recovery."""
import hashlib,json,os,pathlib,re,shlex,subprocess,sys,tarfile,tempfile
root,out,evidence,package,original=map(pathlib.Path,sys.argv[1:])
records=json.loads((evidence/'evidence-manifest.json').read_text())
for name in ('installed.tar','final-on-config.conf'):
 data=(evidence/name).read_bytes();record=records[name]
 assert len(data)==record['bytes'] and hashlib.sha256(data).hexdigest()==record['sha256']
target=pathlib.Path(tempfile.mkdtemp(prefix='trial8-qualification-',dir=out))
with tarfile.open(evidence/'installed.tar') as archive:
 for m in archive:
  name=pathlib.PurePosixPath(m.name)
  assert not name.is_absolute() and '..' not in name.parts
  dest=target/str(name)
  assert all(not p.is_symlink() for p in dest.parents if p!=target.parent)
  dest.parent.mkdir(parents=True,exist_ok=True)
  if m.isdir():dest.mkdir(exist_ok=True);dest.chmod(m.mode)
  elif m.isfile():dest.write_bytes(archive.extractfile(m).read());dest.chmod(m.mode)
  elif m.issym():dest.symlink_to(m.linkname)
  else:raise AssertionError(('unsupported archive type',m.name,m.type))
# The final operator On change happened after installed.tar; only the private
# active-config copy is advanced to that independently hashed evidence file.
(target/'var/hda/4vrs/config/gateway.conf').write_bytes((evidence/'final-on-config.conf').read_bytes())
for rel in ('etc/rc.d/rc0.d','etc/rc.d/rc6.d','var/hda/4vrs/run','var/hda/4vrs/log'):
 (target/rel).mkdir(parents=True,exist_ok=True)
sources=re.findall(r'"\$root/(src/[^"\s]+\.c)"',(root/'tools/build-gateway-application.sh').read_text())
exe=out/'qualification-archive'
cmd=[os.environ.get('CC','cc'),*shlex.split(os.environ.get('CFLAGS','-std=c99 -O2 -g -Wall -Wextra -Werror')),'-I'+str(root/'src'),str(root/'tests/installer/qualification_archive.c'),*[str(p) for p in sorted((root/'src/installer').glob('*.c')) if p.name!='main.c'],*[str(root/p) for p in sources[1:]],'-lrt','-Wl,--wrap=fsync','-o',str(exe)]
subprocess.run(cmd,check=True)
subprocess.run([str(exe),str(target),str(package),str(original)],check=True,timeout=120)
print('Verified exact archived trial8 files plus final On snapshot; originals read-only')
