"""No target/userland commands: native bounded reads, simulated proc, empty PATH."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time

root,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
event='qualification=publication-v1\nboundary=publish-application\npid=5272\nstarttime=419340\nexit=77\n'
def stat(state='S',start='419340',pid='5272'):
    return pid+' (name with ) spaces) '+state+' '+' '.join(['0']*18+[start,'0','0'])+'\n'
count=0
for mode,extra in [('normal',[]),('ubsan',['-fsanitize=undefined','-fno-sanitize-recover=all'])]:
    exe=out/('observer-'+mode)
    subprocess.run(['cc','-std=c99','-O1','-g','-Wall','-Wextra','-Werror',*extra,'-DQUALIFICATION_OBSERVER_TEST',str(root/'tools/qualification-observer.c'),'-lrt','-o',str(exe)],check=True)
    for scenario,expected in [('absent',0),('zombie',0),('reused',4),('live',3),('wrong-pid',2),('bad-stat',2),('stat-missing',2),('proc-missing',2),('stale',1),('missing-event',1),('duplicate',2),('extra',2),('space',2),('large',2),('nul',2),('link',2),('missing-before',2),('fifo',2),('late-event',0),('live-then-absent',0),('event-replaced',2)]:
        with tempfile.TemporaryDirectory(dir=out) as tmp:
            p=Path(tmp);before=p/'before';current=p/'event';proc=p/'proc';proc.mkdir()
            before.write_text(event if scenario=='stale' else '');current.write_text(event)
            if scenario in ('zombie','reused','live','wrong-pid','bad-stat','stat-missing','live-then-absent','event-replaced'):
                d=proc/'5272';d.mkdir()
                if scenario!='stat-missing':
                    (d/'stat').write_text('broken' if scenario=='bad-stat' else stat('Z' if scenario=='zombie' else 'S','1' if scenario=='reused' else '419340','123' if scenario=='wrong-pid' else '5272'))
            if scenario=='proc-missing':proc.rmdir()
            if scenario=='duplicate':current.write_text(event+'pid=5272\n')
            if scenario=='extra':current.write_text(event.replace('exit=77','exit=770'))
            if scenario=='space':current.write_text(event.replace('\n','\n\n',1))
            if scenario=='large':current.write_text('x'*4096)
            if scenario=='nul':current.write_bytes(event.encode()+b'\0')
            if scenario=='link':current.unlink();current.symlink_to(before)
            if scenario=='fifo':current.unlink();os.mkfifo(current)
            if scenario=='missing-before':before.unlink()
            if scenario in ('missing-event','late-event'):current.unlink()
            timer=None
            if scenario=='late-event':timer=threading.Timer(.2,lambda:current.write_text(event))
            if scenario=='live-then-absent':timer=threading.Timer(.2,lambda:((proc/'5272/stat').unlink(),(proc/'5272').rmdir()))
            if scenario=='event-replaced':timer=threading.Timer(.2,lambda:current.write_text(event.replace('419340','419341')))
            if timer:timer.start()
            begin=time.monotonic();r=subprocess.run([str(exe),str(before),str(current),str(proc)],env={**os.environ,'PATH':''},capture_output=True,timeout=3)
            if timer:timer.join()
            assert r.returncode==expected,(mode,scenario,r.returncode,r.stdout,r.stderr)
            assert time.monotonic()-begin<3 and len(r.stdout)+len(r.stderr)<1024
            count+=1
    # Also interpret a real Linux proc record without signaling its process.
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        p=Path(tmp);(p/'before').write_text('')
        child=subprocess.Popen([sys.executable,'-c','import time; time.sleep(2)'])
        start=Path('/proc/'+str(child.pid)+'/stat').read_text().rsplit(') ',1)[1].split()[19]
        (p/'event').write_text(event.replace('pid=5272','pid='+str(child.pid)).replace('starttime=419340','starttime='+start))
        r=subprocess.run([str(exe),str(p/'before'),str(p/'event'),'/proc'],env={**os.environ,'PATH':''},capture_output=True,timeout=3)
        child.wait(timeout=4)
        assert r.returncode==3,(r.returncode,r.stdout,r.stderr)
        count+=1
    # Wrapper uses only shell builtins. Missing/non-executable native tool fails;
    # a command returning nonzero is never converted to successful observation.
    for result in (None,7,126):
        with tempfile.TemporaryDirectory(dir=out) as tmp:
            p=Path(tmp);wrapper=p/'watch.sh';wrapper.write_bytes((root/'tools/watch-installer-qualification.sh').read_bytes())
            if result is not None:
                binary=p/'4vrs-qualification-observer';binary.write_text('#!/bin/sh\nexit 7\n');binary.chmod(0o600 if result==126 else 0o700)
            r=subprocess.run(['/bin/sh',str(wrapper)],cwd=p,env={**os.environ,'PATH':''},capture_output=True)
            assert r.returncode==(127 if result is None else result),r
            count+=1
print('native observer:',count,'cases passed; normal/UBSan; empty PATH; no target access')
