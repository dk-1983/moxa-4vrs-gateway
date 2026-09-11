"""Affected host integration suites in an isolated Linux build environment."""
from pathlib import Path
import os,subprocess,sys
root,out=map(Path,sys.argv[1:3]);mode=sys.argv[3];out.mkdir(parents=True,exist_ok=True)
env=dict(os.environ)
if mode=='ubsan':env['CFLAGS']='-std=c99 -O1 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all'
for name in ['network-integration','network-application','dhcp','panel']:
    r=subprocess.run(['sh',str(root/('tools/run-host-'+name+'-tests.sh')),str(root),str(out/name)],capture_output=True,env=env,timeout=240)
    (out/(name+'.log')).write_bytes(r.stdout+r.stderr)
    print(mode,name,'exit',r.returncode,flush=True)
    if r.returncode:print((r.stdout+r.stderr).decode(errors='replace'));sys.exit(r.returncode)
