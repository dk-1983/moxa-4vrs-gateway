"""Build separate generic-target workers; legacy bootstrap builds remain separate."""
from pathlib import Path
import subprocess,sys
root,tls,previous,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
for name,mode,fixture in [('4vrs-cf-rng-worker','host',False),('wizard-fixture','host',True),('wizard-fixture-ubsan','ubsan',True)]:
 flags=['-std=gnu99','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-DCF_GENERIC_TARGET',
        '-DMBEDTLS_CONFIG_FILE="web/rng_config.h"','-I'+str(root/'src'),'-I'+str(tls/'include')]
 extra=[]
 if fixture:
  flags+=['-DCF_FIXTURE'];extra=[str(root/'tests/web/cf_public_random.c'),*['-Wl,--wrap='+x for x in ['getrandom','write','fsync','close','renameat2']]]
 if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
 subprocess.run(['gcc',*flags,str(root/'src/host/cf_rng_writer.c'),*extra,str(previous/mode/'rng-objects/librng.a'),'-o',str(out/name)],check=True)
