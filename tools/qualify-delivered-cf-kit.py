"""Exercise the distributed ZIP after documented unzip, never storage media."""
from pathlib import Path
import subprocess,sys,tempfile,json,hashlib,importlib.util,os
archive=Path(sys.argv[1]).resolve();report=Path(sys.argv[2])
sha=lambda b:hashlib.sha256(b).hexdigest()
with tempfile.TemporaryDirectory(prefix='delivered-kit-',dir='/opt') as tmp:
    kit=Path(tmp);kit.chmod(0o755)
    subprocess.run(['unzip','-q',archive,'-d',kit],check=True)
    sys.path.insert(0,str(kit))
    spec=importlib.util.spec_from_file_location('delivered_wizard',kit/'cf-wizard.py');w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
    outer=json.loads((kit/'MANIFEST.json').read_text());inner=json.loads((kit/'manifest.json').read_text())['files']
    assert set(outer)==set(inner)|{'manifest.json','SHA256SUMS'}
    for name,item in outer.items():
        data=(kit/name).read_bytes();assert item=={'bytes':len(data),'sha256':sha(data)}
        if name in inner:assert item==inner[name]
    subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=kit,stdout=subprocess.DEVNULL,check=True)
    w.validate_installation(kit)
    payload=w.package.load(kit/'gateway.tar.gz');assert len(payload)==11
    assert os.access(kit/'4vrs-cf-rng-worker',os.X_OK)
    worker=subprocess.run([kit/'4vrs-cf-rng-worker'],capture_output=True)
    assert worker.returncode!=0 and worker.returncode>=0  # Executed, refused missing device arguments.
    original=(kit/'gateway.tar.gz').read_bytes();bad=bytearray(original);bad[len(bad)//2]^=1;(kit/'gateway.tar.gz').write_bytes(bad)
    try:w.package.load(kit/'gateway.tar.gz')
    except ValueError:pass
    else:raise AssertionError('damaged archive accepted')
    try:w.validate_installation(kit)
    except Exception:pass
    else:raise AssertionError('damaged kit accepted')
    result={'result':'PASS','zip_sha256':sha(archive.read_bytes()),'gateway_sha256':sha(original),'pin':w.package.ARCHIVE_SHA256,'unzip_worker_executable':True,'worker_no_arguments_exit':worker.returncode,'both_manifests_equal':True,'package_load_from_unpacked_zip':True,'corrupt_archive_rejected':True,'corrupt_kit_rejected':True,'no_device_access':True}
report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
