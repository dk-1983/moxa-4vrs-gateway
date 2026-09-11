"""Build native Ubuntu-compatible worker using the pinned RNG SHA256 library."""
from pathlib import Path
import subprocess,sys
root,tls,previous,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
for name,mode,fixture in [('4vrs-cf-rng-worker','host',False),('worker-fixture','host',True),('worker-fixture-ubsan','ubsan',True)]:
 flags=['-std=gnu99','-O2','-Wall','-Wextra','-Wno-misleading-indentation',
        '-DMBEDTLS_CONFIG_FILE="web/rng_config.h"','-I'+str(root/'src'),'-I'+str(tls/'include')]
 extra=[]
 if fixture:flags+=['-DCF_FIXTURE'];extra=[str(root/'tests/web/cf_public_random.c'),*['-Wl,--wrap='+x for x in ['getrandom','write','fsync','close','renameat2']]]
 if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
 subprocess.run(['gcc',*flags,str(root/'src/host/cf_rng_writer.c'),*extra,str(previous/mode/'rng-objects/librng.a'),'-o',str(out/name)],check=True)
 if fixture:
  subprocess.run(['gcc',*flags,str(root/'tests/web/test_cf_target_codec.c'),str(previous/mode/'rng-objects/librng.a'),'-o',str(out/('codec-'+mode))],check=True)
