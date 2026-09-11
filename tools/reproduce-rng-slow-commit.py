from pathlib import Path
import subprocess,sys
root=Path('/workspace/source-host');out=Path('/workspace/build/rng-slow-commit-v1-20260911');out.mkdir(exist_ok=True)
mode=sys.argv[1];variant=sys.argv[2] if len(sys.argv)>2 else 'host'
probe=out/'slow-commit-fixture.so'
subprocess.run(['cc','-shared','-fPIC',root/'tests/web/rng_slow_commit_fixture.c','-ldl','-o',probe],check=True)
build=Path('/workspace/build/rng-autonomous-v1-20260911')/variant if mode=='baseline' else out/variant
with (out/(mode+'-slow-'+variant+'.log')).open('wb') as log:
    args=['python3',root/'tests/web/test_rng_stop_rotation.py',build,probe,out/(mode+'-slow-'+variant+'.json')] if mode=='rotation' else ['python3',root/'tests/web/test_rng_slow_commit.py',build,probe,mode,out/(mode+'-slow-'+variant+'.json')]
    result=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT)
if result.returncode:print((out/(mode+'-slow-'+variant+'.log')).read_text()[-4000:])
else:print((out/(mode+'-slow-'+variant+'.json')).read_text())
sys.exit(result.returncode)
