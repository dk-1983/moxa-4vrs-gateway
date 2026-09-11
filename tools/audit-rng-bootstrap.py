"""Read only public SSH implementation metadata from a verified vendor archive.
Never extracts private keys, accesses devices or claims active binary identity.
"""
from pathlib import Path
import hashlib,io,json,re,sys,tarfile
p=Path(sys.argv[1]);out=Path(sys.argv[2]);h=hashlib.sha256(p.read_bytes()).hexdigest()
assert h=='88d50f23c0bbec7af3cf0a72837454732e980e4718a5eeda0028809b0625949c'
records=[]
with tarfile.open(p) as archive:
    for member in archive:
        if member.name!='UC-7400P/vendors/rootdiskdir.tar.bz2':continue
        data=archive.extractfile(member).read()
        with tarfile.open(fileobj=io.BytesIO(data)) as root:
            for m in root:
                name=m.name.lstrip('./')
                if name.startswith('rootdiskdir/'):name=name[len('rootdiskdir/'):]
                if not m.isfile():continue
                if name not in ['usr/sbin/sshd','sbin/sshd','usr/bin/ssh','etc/init.d/ssh','etc/rc.d/init.d/ssh','etc/default/ssh'] and not re.fullmatch(r'(usr/)?lib/libcrypto[^/]*',name):continue
                b=root.extractfile(m).read()
                versions=sorted(set(x.decode('ascii') for x in re.findall(rb'(?:OpenSSH_[0-9][A-Za-z0-9._-]*|OpenSSL [0-9][A-Za-z0-9. -]{0,40})',b)))
                records.append({'name':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'version_strings':versions})
result={'archive_sha256':h,'public_metadata':records,'private_keys_read':False,'hardware_access':False,
        'running_binary_identity_proven':False,'host_key_generation_proven':False,'ssh_kex_entropy_proven':False,
        'production_channel_qualified':False,'reason':'Archive metadata cannot establish active key provenance or runtime entropy initialization on the device.'}
out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
