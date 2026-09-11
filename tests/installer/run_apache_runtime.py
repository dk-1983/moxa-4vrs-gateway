"""Private container only: install a temporary fake /bin/httpd, then remove it."""
import os, pathlib, shlex, subprocess, sys
root,out=map(pathlib.Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
target=pathlib.Path('/bin/httpd')
assert pathlib.Path('/.dockerenv').exists() and not target.exists() and not target.is_symlink()
assert not pathlib.Path('/tmp/4vrs-apache-fixture.pid').exists()
flags=shlex.split(os.environ.get('CFLAGS','-std=c99 -O2 -Wall -Wextra -Werror'))
def build(name,sources):
 subprocess.run(['cc',*flags,'-I'+str(root/'src'),*[str(root/s) for s in sources],'-lrt','-o',str(out/name)],check=True)
build('apache-fixture',['tests/installer/apache_fixture.c'])
build('test-apache-runtime',['tests/installer/test_apache_runtime.c','src/installer/install_apache.c','src/installer/install_process.c','src/installer/install_transaction.c','src/installer/install_files.c','src/installer/install_digest.c'])
target.symlink_to(out/'apache-fixture')
try:
 subprocess.run([out/'test-apache-runtime'],check=True,timeout=50)
finally:
 if pathlib.Path('/tmp/4vrs-apache-fixture.pid').exists():subprocess.run([target,'-f','/etc/apache/httpd.conf','-k','stop'],timeout=12,check=False)
 assert target.is_symlink() and target.readlink()==out/'apache-fixture'
 target.unlink()
