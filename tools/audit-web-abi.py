"""Audit new executable imports against PRESERVED libraries. Never executes ARM."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
libs=Path(sys.argv[1]);out=Path(sys.argv[2]);binaries=list(map(Path,sys.argv[3:]))
def read(*args):return subprocess.check_output(['readelf',*map(str,args)],text=True)
exports=set();providers={}
for path in sorted(libs.iterdir()):
    if not path.is_file():continue
    providers[path.name]=hashlib.sha256(path.read_bytes()).hexdigest()
    for line in read('--dyn-syms','--wide',path).splitlines():
        columns=line.split()
        if len(columns)>=8 and columns[0].endswith(':') and columns[6]!='UND':
            name=columns[7].replace('@@','@');exports.add(name);exports.add(name.split('@')[0])
reports=[]
for path in binaries:
    header=read('-h','-l','-d',path)
    assert 'ELF32' in header and 'big endian' in header and '0x4000002' in header and '/lib/ld-linux.so.3' in header
    needed=re.findall(r'Shared library: \[([^]]+)\]',header)
    assert all(name in providers for name in needed),needed
    imports=[];weak=[]
    for line in read('--dyn-syms','--wide',path).splitlines():
        columns=line.split()
        if len(columns)<8 or not columns[0].endswith(':'):continue
        if columns[6]=='UND' or '@GLIBC_' in columns[7]:
            (weak if columns[4]=='WEAK' else imports).append(columns[7].replace('@@','@'))
    missing=sorted(set(imports)-exports)
    reports.append({'path':str(path),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                    'needed':needed,'imports':imports,'weak_unresolved':sorted(set(weak)-exports),'missing':missing,
                    'size':subprocess.check_output(['size',str(path)],text=True)})
    assert not missing,missing
out.write_text(json.dumps({'providers':providers,'binaries':reports},indent=2)+'\n')
print('ABI: all strong imports and versioned copy relocations available;',len(reports),'ARM executables')
