"""Verified published managed archive through native orchestration with fake hardware."""
import hashlib, json, os, pathlib, re, shlex, subprocess, sys, tarfile, tempfile
root,out,evidence,package=map(pathlib.Path,sys.argv[1:5]); mode=sys.argv[5]
out.mkdir(parents=True,exist_ok=True)
# Published v2026.01.01 captures, explicitly selected by caller. No live access.
record=json.loads((evidence/'manifest.json').read_text())['after.tar']
archive_path=evidence/'after.tar'
data=archive_path.read_bytes()
assert len(data)==record['size'] and hashlib.sha256(data).hexdigest()==record['sha256']
print('verified preserved after.tar:',record['sha256'],flush=True)
sources=re.findall(r'"\$root/(src/[^"\s]+\.c)"',(root/'tools/build-gateway-application.sh').read_text())
exe=out/'managed-archive-probe'
subprocess.run([os.environ.get('CC','cc'),*shlex.split(os.environ.get('CFLAGS','-std=c99 -O2 -g -Wall -Wextra -Werror')),'-I'+str(root/'src'),str(root/'tests/installer/managed_archive_probe.c'),*[str(p) for p in sorted((root/'src/installer').glob('*.c')) if p.name!='main.c'],*[str(root/p) for p in sources[1:]],'-lrt','-Wl,--wrap=fsync','-o',str(exe)],check=True)
for scenario in ([mode] if mode=='refuse' else ['complete','rollback']):
 target=pathlib.Path(tempfile.mkdtemp(prefix='managed-archive-',dir=out))
 with tarfile.open(archive_path) as archive:
  # Fresh private root. Reject paths before manually extracting; never traverse links.
  for m in archive:
   name=pathlib.PurePosixPath(m.name)
   assert not name.is_absolute() and '..' not in name.parts
   dest=target/str(name)
   assert all(not p.is_symlink() for p in dest.parents if p!=target.parent)
   dest.parent.mkdir(parents=True,exist_ok=True)
   if m.isdir(): dest.mkdir(exist_ok=True); dest.chmod(m.mode)
   elif m.isfile(): dest.write_bytes(archive.extractfile(m).read()); dest.chmod(m.mode)
   elif m.issym(): dest.symlink_to(m.linkname)
   else: raise AssertionError(('unsupported archive type',m.name,m.type))
 for rel in ('etc/rc.d/rc0.d','etc/rc.d/rc6.d','var/hda/4vrs/run','var/hda/4vrs/log'):
  (target/rel).mkdir(parents=True,exist_ok=True)
 subprocess.run([str(exe),str(target),str(package),scenario],check=True)
