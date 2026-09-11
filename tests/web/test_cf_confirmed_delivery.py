"""Offline smoke test with only accompaniment ZIP and this driver mounted."""
from pathlib import Path
import hashlib,importlib.util,json,subprocess,sys,tempfile,zipfile
with tempfile.TemporaryDirectory() as temp:
    root=Path(temp)
    with zipfile.ZipFile(sys.argv[1]) as z:
        assert len(z.namelist())==15
        assert not any(Path(n).name in ['state','pending','seed','inspection.json','trial-policy'] for n in z.namelist())
        for i in z.infolist():
            assert not i.filename.startswith('/') and '..' not in Path(i.filename).parts
            p=root/i.filename;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(i));p.chmod((i.external_attr>>16)&0o777)
    for path in [root,root/'kit']:
        subprocess.run(['sha256sum','-c','SHA256SUMS'],cwd=path,check=True,stdout=subprocess.PIPE)
    kit=root/'kit';target=json.loads((kit/'target-confirmation.json').read_text())
    assert target['confirmed'] is False and target['cf_uuid']=='2ad4f17a-0eb1-bc47-8c4e-b23d70f39b52'
    assert hashlib.sha256((kit/'4vrs-cf-rng-worker').read_bytes()).hexdigest()=='2a87bf2354215e727e9e0c936818a333d5ce4feddce0201219eaa6c28212b69d'
    assert hashlib.sha256(next((root/'baseline').iterdir()).read_bytes()).hexdigest()=='1fd33434f3a39135b0a38154c0f78f498a7c2b52a3edc79d67c5115485ee4f83'
    subprocess.run(['python3',kit/'cf-bootstrap.py','--help'],check=True,stdout=subprocess.PIPE)
    assert subprocess.run([kit/'4vrs-cf-rng-worker'],capture_output=True).returncode==2
    spec=importlib.util.spec_from_file_location('cf',kit/'cf-bootstrap.py');cf=importlib.util.module_from_spec(spec);spec.loader.exec_module(cf)
    media={'uuid':target['cf_uuid'].replace('-',''),'disk_bytes':target['disk_bytes'],'partition_bytes':target['partition_bytes']}
    cf.target_confirmation(target,media,require_confirmed=False)
    try:cf.target_confirmation(target,media)
    except ValueError:pass
    else:raise AssertionError('unconfirmed write allowed')
print('PASS accompaniment: 15 entries; both checksum sets; baseline/worker unchanged; real UUID with confirmed:false; inspect gate works, write gate refuses; no receipt/state/seed; no repository needed')
