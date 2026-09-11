"""Only public fixtures: real ext3 image metadata plus POSIX commit fault tests.
Images are never loop-mounted; native production worker must reject these files.
"""
import hashlib,importlib.util,json,os,shutil,struct,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('cf',root/'tools/cf-bootstrap.py');cf=importlib.util.module_from_spec(spec);spec.loader.exec_module(cf)
worker=Path(sys.argv[1]).resolve();production=Path(sys.argv[2]).resolve();out=Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
UUID='00112233445566778899aabbccddeeff';PUBLIC=bytes(255 if i==5 else i for i in range(32));checks=[]
def check(name,value):
 assert value,name
 checks.append(name);print('PASS',name,flush=True)
def refused(name,fn):
 try:fn()
 except (ValueError,OSError):check(name,True)
 else:raise AssertionError(name)
with tempfile.TemporaryDirectory(prefix='cf-public-',dir=out) as tmp:
 d=Path(tmp);part=d/'partition.img';disk=d/'disk.img'
 with part.open('wb') as f:f.truncate(32*1024*1024)
 subprocess.run(['mkfs.ext3','-q','-F','-U','00112233-4455-6677-8899-aabbccddeeff','-E','hash_seed=00112233-4455-6677-8899-aabbccddeeff',part],check=True)
 header=bytearray(512);header[510:]=b'\x55\xaa';header[446+4]=0x83;struct.pack_into('<II',header,454,2048,part.stat().st_size//512)
 with disk.open('wb') as f:f.write(header);f.truncate(33*1024*1024)
 pfd=os.open(part,os.O_RDONLY);dfd=os.open(disk,os.O_RDONLY)
 sb=os.pread(pfd,1024,1024);meta=cf.superblock(sb)
 check('actual ext3 image clean UUID',meta['uuid']==UUID and meta['clean'])
 check('actual MBR geometry',cf.mbr(bytes(header),1048576,part.stat().st_size)=='00000000')
 for off,label in [(56,'bad magic'),(104,'UUID mutation changes binding')]:
  bad=bytearray(sb);bad[off]^=255
  if off==56:refused(label,lambda:cf.superblock(bad))
  else:check(label,cf.superblock(bad)['uuid']!=UUID)
 bad=bytearray(sb);struct.pack_into('<I',bad,96,0x40);refused('ext4 features refused',lambda:cf.superblock(bad))
 bad=bytearray(sb);struct.pack_into('<H',bad,58,2);refused('filesystem errors refused',lambda:cf.superblock(bad))
 bad=bytearray(sb);struct.pack_into('<I',bad,96,6);check('journal recovery required is not clean',not cf.superblock(bad)['clean'])
 bad=bytearray(header);bad[462+4]=0x83;refused('second MBR partition refused',lambda:cf.mbr(bytes(bad),1048576,part.stat().st_size))
 media=dict(uuid=UUID,disk_bytes=disk.stat().st_size,partition_bytes=part.stat().st_size)
 confirm=dict(confirmed=True,address=cf.ADDRESS,mac=cf.MAC,cf_uuid=UUID,**{k:media[k] for k in ['disk_bytes','partition_bytes']})
 cf.target_confirmation(confirm,media);check('confirmed target matching UUID and geometry',True)
 for key,value in [('confirmed',False),('address','10.0.2.15:22'),('mac','00:90:e8:1f:4c:dd'),('cf_uuid','f'*32),('disk_bytes',1),('partition_bytes',1)]:
  bad=dict(confirm);bad[key]=value;refused('target gate '+key,lambda:cf.target_confirmation(bad,media))
 line='40 20 8:17 / /mnt/card ro,nosuid,nodev,noexec - ext3 /dev/example ro,norecovery\n'
 cf.mounts(line,'8:17','/mnt/card',False);check('RO mount inspection',True)
 for name,text in [('duplicate',line*2),('wrong path',line.replace('/mnt/card','/mnt/other')),('subdirectory',line.replace(' / /mnt',' /sub /mnt')),('unsafe flags',line.replace(',nodev','')),('journal replay',line.replace(',norecovery',''))]:
  refused('mount '+name,lambda:cf.mounts(text,'8:17','/mnt/card',False))
 cf.mounts(line.replace('ro,','rw,').replace(',norecovery',''),'8:17','/mnt/card',True);check('explicit RW journal mount',True)
 receipt=dict(schema=1,clean_readonly_inspection=True,**media,address=cf.ADDRESS,mac=cf.MAC,
              disk='/dev/fixture',partition='/dev/fixture1',mount='/mnt/card',disk_sysfs='/fixture',
              partition_sysfs='/fixture/fixture1',reader='05e3:0743',offset=1048576,disk_id='00000000')
 cf.compare(receipt,receipt);changed=dict(receipt);changed['uuid']='f'*32;refused('receipt swap',lambda:cf.compare(receipt,changed))
 refused('incomplete receipt',lambda:cf.compare({'schema':1,'clean_readonly_inspection':True},receipt))
 def case(name):
  path=d/name;path.mkdir(mode=0o755);(path/'4vrs').mkdir();(path/'4vrs/log\r').mkdir();(path/'4vrs/config').write_bytes(b'public user data\n');return path
 def call(path,mode='prepare',env=None,mac=cf.MAC,uuid=UUID,exe=worker):
  rfd=os.open(path,os.O_RDONLY|os.O_DIRECTORY)
  try:return subprocess.run([exe,mode,str(rfd),str(pfd),str(dfd),mac,uuid,str(disk.stat().st_size),str(part.stat().st_size),'confirmed-moxa1'],pass_fds=(rfd,pfd,dfd),capture_output=True,env={**os.environ,**(env or {})},timeout=5)
  finally:os.close(rfd)
 normal=case('normal');r=call(normal);check('native exact-state preparation',r.returncode==0)
 state=normal/'4vrs-rng/state';record=state.read_bytes()
 expected=bytearray(160);expected[:8]=b'4VRSNV01';expected[11]=1
 expected[16:48]=hashlib.sha256(('4vrs-nv-binding-v1:'+cf.MAC).encode()).digest();expected[48:64]=bytes.fromhex(UUID);expected[96:128]=PUBLIC;expected[128:]=hashlib.sha256(expected[:128]).digest()
 check('exact original target schema byte compatibility',record==expected)
 check('native verify',call(normal,'verify').returncode==0)
 modern=bytearray(192);modern[:128]=record[:128];modern[:8]=b'4VRSNV02';modern[15]=8;modern[128:141]=b'production-v1';modern[160:]=hashlib.sha256(modern[:160]).digest()
 state.write_bytes(modern)
 check('schema 2 verification preserves quota and seed',call(normal,'verify').returncode==0 and state.read_bytes()==modern)
 (normal/'4vrs-rng/attempt').write_bytes(b'production-v1\n');(normal/'4vrs-rng/attempt').chmod(0o600)
 check('schema 2 incomplete operation requires service',call(normal,'verify').returncode==1 and state.read_bytes()==modern)
 (normal/'4vrs-rng/attempt').unlink()
 modern[:8]=b'4VRSNV03';modern[128:160]=bytes(32);modern[128:152]=b'production-autonomous-v1\0';modern[15]=20;modern[160:]=hashlib.sha256(modern[:160]).digest()
 state.write_bytes(modern);witness=normal/'4vrs-rng/witness';witness.write_bytes(modern);witness.chmod(0o600)
 check('schema 3 matching witness above eight verifies',call(normal,'verify').returncode==0 and state.read_bytes()==modern)
 bad=bytearray(modern);bad[100]^=1;witness.write_bytes(bad)
 check('schema 3 inconsistent witness refused',call(normal,'verify').returncode==1)
 witness.unlink();state.write_bytes(record)
 check('root directory and state modes',(normal/'4vrs-rng').stat().st_mode&0o777==0o700 and state.stat().st_mode&0o777==0o600)
 check('existing state never reinitialized',call(normal,env={'CF_RANDOM':'forbidden'}).returncode==1 and state.read_bytes()==record)
 check('payload absent from output',PUBLIC not in r.stdout+r.stderr and PUBLIC.hex().encode() not in r.stdout+r.stderr and record[128:].hex().encode() not in r.stdout+r.stderr)
 if len(sys.argv)>4:
  p=case('target-codec');check('codec fixture prepared',call(p).returncode==0)
  r=subprocess.run([sys.argv[4],str(p/'4vrs-rng')],capture_output=True,timeout=5)
  check('unchanged target decoder lifecycle and binding',r.returncode==0)
  print(r.stdout.decode(),end='')
  check('target lifecycle output contains no seed',PUBLIC not in r.stdout+r.stderr)
 for label,kwargs in [('other target',{'mac':'00:90:e8:1f:4c:dd'}),('binding mismatch',{'uuid':'f'*32})]:
  p=case(label);r=call(p,env={'CF_RANDOM':'forbidden'},**kwargs);check(label+' no generation',r.returncode not in [0,99] and not (p/'4vrs-rng').exists())
 p=case('production-image');r=call(p,exe=production);check('production rejects regular-file images without state',r.returncode!=0 and not (p/'4vrs-rng').exists())
 p=case('permissions');p.chmod(0o777);check('unsafe mount-root permissions refuse',call(p,env={'CF_RANDOM':'forbidden'}).returncode==1)
 for label,create in [('empty-directory',lambda p:p.mkdir(mode=0o700)),('rng-symlink',lambda p:p.symlink_to('../4vrs')),('rng-file',lambda p:p.write_text('public'))]:
  p=case(label);create(p/'4vrs-rng')
  check('pre-existing '+label+' forbids generation',call(p,env={'CF_RANDOM':'forbidden'}).returncode==1)
 for label in ['eagain','enosys','eintr','short']:
  p=case('rng-'+label);check('OS source '+label+' fails',call(p,env={'CF_RANDOM':label}).returncode==1 and not (p/'4vrs-rng/state').exists())
 for label in ['short-write','write-error']:
  p=case(label);check(label,call(p,env={'CF_IO':label}).returncode==1 and not (p/'4vrs-rng/state').exists())
 for label in ['fsync-1','fsync-2','fsync-3','fsync-4','fsync-5','close-error','rename-error','rename-collision']:
  p=case(label);check('syscall '+label+' refuses success',call(p,env={'CF_IO':label}).returncode==1)
  check('syscall '+label+' forbids reinitialization',call(p,env={'CF_RANDOM':'forbidden'}).returncode==1)
  if label=='rename-collision':check('concurrent destination never overwritten',(p/'4vrs-rng/state').read_bytes()==b'PUBLIC-existing-state')
  elif label=='fsync-5':check('final directory fsync failure inspectable',call(p,'verify').returncode==0)
  else:check('syscall '+label+' no committed state',not (p/'4vrs-rng/state').exists())
 phases=['precheck','before-mkdir','mkdir','parent-fsync','lock-fsync','random','after-random','created','before-write','written','before-file-fsync','file-fsync','closed','before-rename','renamed','before-directory-fsync','directory-fsync','before-ack']
 for injection in ['CF_FAIL','CF_CRASH']:
  for phase in phases:
   p=case(injection+'-'+phase);r=call(p,env={injection:phase});check(injection+' '+phase,r.returncode==(77 if injection=='CF_CRASH' else 1))
   exists=(p/'4vrs-rng/state').exists();check('commit boundary '+injection+' '+phase,exists==(phases.index(phase)>=phases.index('renamed')))
   if (p/'4vrs-rng').exists():check('no retry '+injection+' '+phase,call(p,env={'CF_RANDOM':'forbidden'}).returncode==1)
   if exists:check('lost acknowledgement verifies '+injection+' '+phase,call(p,'verify').returncode==0)
   check('user data preserved '+injection+' '+phase,(p/'4vrs/config').read_bytes()==b'public user data\n' and (p/'4vrs/log\r').is_dir())
 for label,mutate in [('corrupt',lambda p:p.write_bytes(b'broken')),('hardlink',lambda p:os.link(p,p.with_name('copy'))),('wrong-mode',lambda p:p.chmod(0o644)),('symlink',lambda p:(p.rename(p.with_name('saved')),p.symlink_to('saved'))),('fifo',lambda p:(p.unlink(),os.mkfifo(p,0o600)))]:
  p=case(label);check('fixture prepare '+label,call(p).returncode==0);s=p/'4vrs-rng/state';mutate(s)
  check('invalid state '+label+' refuses verify and prepare',call(p,'verify').returncode==1 and call(p,env={'CF_RANDOM':'forbidden'}).returncode==1)
 for label,mutate in [('missing-lock',lambda p:p.unlink()),('lock-mode',lambda p:p.chmod(0o644)),('lock-hardlink',lambda p:os.link(p,p.with_name('copy')))]:
  p=case(label);check('fixture prepare '+label,call(p).returncode==0);mutate(p/'4vrs-rng/owner.lock')
  check('invalid lock '+label+' refuses verify',call(p,'verify').returncode==1)
 # Public record roundtrip through a real ext3 image; NOT mounted fsync testing.
 public=d/'public-record';public.write_bytes(record)
 commands=d/'debugfs.commands';commands.write_text('mkdir /4vrs-rng\nwrite '+str(public)+' /4vrs-rng/state\n')
 subprocess.run(['debugfs','-w','-f',commands,part],check=True,capture_output=True)
 restored=d/'restored-public';subprocess.run(['debugfs','-R','dump /4vrs-rng/state '+str(restored),part],check=True,capture_output=True)
 check('actual ext3 image record roundtrip',restored.read_bytes()==record)
 os.close(pfd);os.close(dfd)
print('TOTAL',len(checks),'PASS; all RNG input public; no mounted image or physical CF')
