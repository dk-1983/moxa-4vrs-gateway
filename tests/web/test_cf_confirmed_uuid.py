"""Focused public-seed regression for the confirmed UUID; no hardware access."""
from pathlib import Path
import contextlib, hashlib, importlib.util, io, json, os, struct, subprocess, sys, tempfile
from unittest.mock import patch
root, binaries, codec, output = map(Path,sys.argv[1:])
output.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('cf',root/'tools/cf-bootstrap.py')
cf=importlib.util.module_from_spec(spec);spec.loader.exec_module(cf)
uuid='2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52'; raw=bytes.fromhex(uuid.replace('-',''))
checks=[]
def check(name,ok):
    assert ok,name
    checks.append(name);print('PASS',name)
with tempfile.TemporaryDirectory(dir=output) as temp:
    p=Path(temp);part=p/'part.img';disk=p/'disk.img';mount=p/'mount';mount.mkdir()
    with part.open('wb') as f:f.truncate(32*1024*1024)
    with disk.open('wb') as f:f.truncate(33*1024*1024)
    subprocess.run(['mkfs.ext3','-q','-F','-U',uuid,'-E','hash_seed='+uuid,part],check=True)
    with part.open('rb') as f:f.seek(1128);check('superblock exact 16 raw UUID bytes',f.read(16)==raw)
    fds=[os.open(x,os.O_RDONLY) for x in [mount,part,disk]]
    try:
        result=subprocess.run([binaries,'prepare',*map(str,fds),cf.MAC,raw.hex(),str(disk.stat().st_size),str(part.stat().st_size),'confirmed-moxa1'],pass_fds=fds,capture_output=True,check=True)
    finally:
        for fd in fds:os.close(fd)
    record=(mount/'4vrs-rng/state').read_bytes()
    public=bytes(255 if i==5 else i for i in range(32))
    check('only public fixture seed',record[96:128]==public)
    check('MAC binding SHA256',record[16:48]==hashlib.sha256(('4vrs-nv-binding-v1:'+cf.MAC).encode()).digest())
    check('UUID serialized raw without GUID endian swap',record[48:64]==raw and record[48:64]!=raw[:4][::-1]+raw[4:6][::-1]+raw[6:8][::-1]+raw[8:])
    check('generation big endian and digest',record[8:12]==b'\0\0\0\1' and hashlib.sha256(record[:128]).digest()==record[128:])
    result=subprocess.run([codec,mount/'4vrs-rng'],capture_output=True,check=True)
    check('unchanged decoder accepts confirmed binding, advances 1-2-3, rejects mismatch',b'PASS unchanged target codec' in result.stdout)
    # Frontend CLI flow: synthetic capture metadata only, never a delivery receipt.
    media=dict(schema=1,clean_readonly_inspection=True,address=cf.ADDRESS,mac=cf.MAC,uuid=raw.hex(),disk_bytes=4009549824,
               partition_bytes=4008501248,offset=1048576,disk='/dev/public',partition='/dev/public1',mount=str(mount),
               disk_sysfs='/public',partition_sysfs='/public/1',reader='05e3:0743',disk_id='12345678',rng='absent',disk_ro=1,partition_ro=1)
    target=p/'target.json';receipt=p/'inspection.json'
    confirmation=dict(confirmed=False,address=cf.ADDRESS,mac=cf.MAC,cf_uuid=uuid,disk_bytes=4009549824,partition_bytes=4008501248)
    def call(mode,authorize=False):
        args=['cf-bootstrap.py',mode,'--disk','/dev/public','--partition','/dev/public1','--mount',str(mount),
              '--target-confirmation',str(target),'--receipt',str(receipt)]+(['--authorize-write'] if authorize else [])
        with patch.object(sys,'argv',args),patch.object(cf,'capture',return_value=(media,())),patch.object(cf.subprocess,'run') as run,contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            run.return_value.returncode=0
            code=cf.main()
            return code,run.call_count
    target.write_text(json.dumps(confirmation))
    check('inspect works with confirmed false and never invokes worker',call('inspect')==(0,0) and receipt.exists())
    check('prepare false refuses even with write flag and receipt',call('prepare',True)==(1,0))
    confirmation['confirmed']=True;target.write_text(json.dumps(confirmation))
    receipt.unlink();check('prepare true requires receipt',call('prepare',True)==(1,0))
    check('fresh fixture inspect after confirmation',call('inspect')==(0,0))
    check('prepare requires explicit write flag',call('prepare')==(1,0))
    check('all three gates permit dispatch only',call('prepare',True)==(0,1))
    confirmation['cf_uuid']='0eb1bc478c4eb23d70f39b5255433734';target.write_text(json.dumps(confirmation));receipt.unlink()
    check('incorrect historical UUID refuses inspect without receipt',call('inspect')==(1,0) and not receipt.exists())
print('TOTAL',len(checks),'PASS; public seed and synthetic receipts destroyed; production worker unchanged')
