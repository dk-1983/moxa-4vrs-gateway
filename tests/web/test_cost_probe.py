"""Exercise real KDF/certificate code with host-only syscall fault injection."""
import os,subprocess,sys,json
from pathlib import Path
out=Path(sys.argv[1]);passed=[]
def run(name,binary,args,code,need,fault=''):
 env=dict(os.environ,PROBE_FAULT=fault)
 r=subprocess.run([str(out/binary),*args],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=65)
 (out/(name+'.log')).write_text(r.stdout)
 assert r.returncode==code,(name,r.returncode,r.stdout)
 for s in need:assert s in r.stdout,(name,s,r.stdout)
 assert 'runtime error:' not in r.stdout
 passed.append(name)
run('measure','4vrs-web-cost-probe',['--measure'],0,['kdf=pass certificate=pass','rounds=600000','maxrss_units=unverified'])
run('entropy-only','4vrs-web-cost-probe',['--entropy-only'],0,['kdf=skipped certificate=pass'])
for fault,reason,num in [('open','open',13),('eagain','would_block',11),('eintr','interrupted',4),('eio','read_error',5),('eof','eof',0),('short','short_read',0)]:
 run('fault-'+fault,'fault-probe',['--measure'],1,['stage=kdf state=finished','kdf=pass certificate=fail','reason='+reason,'errno='+str(num),'mbedtls_ctr_drbg_seed'],fault)
run('kdf-with-no-entropy','fault-probe',['--kdf-only'],0,['kdf=pass certificate=skipped'],'open')
run('entropy-with-no-kdf','fault-probe',['--entropy-only'],1,['kdf=skipped certificate=fail','reason=would_block'],'eagain')
run('priority-refused','fault-probe',['--measure'],2,['stage=bounds reason=setup errno=1'],'priority')
run('alarm','fault-probe',['--entropy-only'],124,['stage=certificate reason=bound_expired'],'alarm')
run('invalid-argument','4vrs-web-cost-probe',['--serve'],2,['usage:'])
run('tls-unsynced','tls/tls-probe',['--unsynced','/dev/null'],3,['clock unsynced'])
run('tls-injected-failure','tls/tls-probe',['--entropy-fail','/dev/null'],1,['reason=injected_failure','mbedtls_ctr_drbg_seed'])
(out/'test-results.json').write_text(json.dumps({'passed':passed},indent=2)+'\n')
print(len(passed),'cost probe checks PASS')
