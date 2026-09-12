"""Build production NV owner/helper with separate pinned Mbed TLS profile."""
from pathlib import Path
import subprocess,sys
root,tls,out=map(Path,sys.argv[1:4]);mode=sys.argv[4];out.mkdir(parents=True,exist_ok=True)
cc='/usr/local/mxscaleb/bin/mxscaleb-gcc' if mode=='linux24' else '/usr/local/xscale_be/bin/xscale_be-gcc' if mode in ('target','linux24') else 'cc'
ar='/usr/local/mxscaleb/bin/mxscaleb-ar' if mode=='linux24' else '/usr/local/xscale_be/bin/xscale_be-ar' if mode in ('target','linux24') else 'ar'
flags=['-std=c99','-Os','-Wall','-Wextra','-DMBEDTLS_CONFIG_FILE="web/rng_config.h"','-I'+str(root/'src'),'-I'+str(tls/'include'),'-I'+str(tls/'library')]
if mode=='linux24':flags=[('-W' if f=='-Wextra' else f) for f in flags]+['-DFOURVRS_LINUX24']
if mode in ('target','linux24'):flags+=['-mcpu=xscale','-mbig-endian','-msoft-float']
else:flags+=['-DWEB_HOST_TEST','-Wno-misleading-indentation']
if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
obj=out/'rng-objects';obj.mkdir(exist_ok=True);objects=[]
for p in sorted((tls/'library').glob('*.c')):
    o=obj/(p.stem+'.o');subprocess.run([cc,*flags,'-c',str(p),'-o',str(o)],check=True);objects.append(str(o))
subprocess.run([ar,'rcs',str(obj/'librng.a'),*objects],check=True)
subprocess.run([cc,*flags,str(root/'src/web/rng_main.c'),str(root/'src/web/rng_nv.c'),str(obj/'librng.a'),'-lrt','-o',str(out/'4vrs-rng')],check=True)
