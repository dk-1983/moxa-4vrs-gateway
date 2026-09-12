"""Package/selector tests using real candidate ELF, never install or access devices."""
from pathlib import Path
import importlib.util,subprocess,sys,tempfile,tarfile,io,hashlib
root,a24,a26=map(Path,sys.argv[1:]);spec=importlib.util.spec_from_file_location('universal',root/'tools/build-universal-installer.py')
u=importlib.util.module_from_spec(spec);spec.loader.exec_module(u)
version='v2026.02.03';launcher=root/'deploy/4vrs-install-universal'
function=launcher.read_text().split('select_platform() {',1)[1].split('\n}',1)[0]
function='select_platform() {'+function+'\n}\nselect_platform "$1" "$2"\n'
cases=[('armv5teb','2.4.18_mvl30-ixdp425','linux24'),('armv5teb','2.6.10_dev-ixdp42x-arm_xscale_be','linux26'),('armv5tel','2.4.18_mvl30-ixdp425',None),('x86_64','2.6.10_dev-ixdp42x-arm_xscale_be',None),('armv5teb','2.6.100_xscale_be',None),('armv5teb','2.4.18_other',None)]
for machine,kernel,want in cases:
 p=subprocess.run(['sh','-c',function,'test',machine,kernel],capture_output=True,text=True)
 assert (p.returncode==0 and p.stdout.strip()==want) if want else p.returncode!=0
for path,platform in [(a24,'linux24'),(a26,'linux26')]:u.load(path,platform,version)
for path,platform in [(a24,'linux26'),(a26,'linux24')]:
 try:u.load(path,platform,version)
 except ValueError:pass
 else:raise AssertionError('wrong architecture accepted')
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 x=u.build(version,a24,a26,launcher,d/'a.tar.gz');y=u.build(version,a24,a26,launcher,d/'b.tar.gz');assert x==y
 try:u.build(version,a24,a26,launcher,d/'a.tar.gz')
 except FileExistsError:pass
 else:raise AssertionError('existing output overwritten')
 for kind in ('corrupt','duplicate','traversal'):
  target=d/(kind+'.tar.gz')
  with tarfile.open(a24,'r:gz') as source,tarfile.open(target,'w:gz') as out:
   for member in source:
    data=source.extractfile(member).read()
    if member.name.endswith('/4vrs-gateway'):
     if kind=='corrupt':data=data[:-1]+bytes([data[-1]^1])
     if kind=='traversal':member.name='../4vrs-gateway'
     out.addfile(member,io.BytesIO(data))
     if kind=='duplicate':out.addfile(member,io.BytesIO(data))
    else:out.addfile(member,io.BytesIO(data))
  try:u.load(target,'linux24',version)
  except ValueError:pass
  else:raise AssertionError(kind+' accepted')
print('PASS: platform selection, both real ELF profiles, mixed-profile rejection, corruption, duplicate, traversal, reproducibility, existing-output preservation')
