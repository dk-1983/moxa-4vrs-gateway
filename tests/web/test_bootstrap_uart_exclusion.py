"""Disposable Linux container only: synthetic ttyM nodes alias a newly made PTY.
Refuses to run if any ttyM node already exists. Never touches a real UART.
"""
import fcntl,os,pty,select,signal,stat,sys,termios,time
from pathlib import Path
binary=str(Path(sys.argv[1]).resolve())
assert Path('/.dockerenv').exists(), 'requires the disposable Docker fixture environment'
nodes=[Path('/dev/ttyM'+str(i)) for i in range(8)]
assert all(not p.exists() and not p.is_symlink() for p in nodes)
for node in [None,*nodes]:
 master,slave=pty.openpty();name=os.ttyname(slave);saved=termios.tcgetattr(slave)
 if node is not None:os.mknod(node,stat.S_IFCHR|0o600,os.fstat(slave).st_rdev)
 pid=os.fork()
 if not pid:
  os.close(master);os.setsid();fcntl.ioctl(slave,termios.TIOCSCTTY,0)
  for fd in range(3):os.dup2(slave,fd)
  if slave>2:os.close(slave)
  os.execv(binary,[binary,'probe',name,'/not-used-for-probe','--reserved-console'])
 os.close(slave)
 try:
  end=time.monotonic()+4;received=False
  while time.monotonic()<end:
   child,status=os.waitpid(pid,os.WNOHANG)
   if child:pid=0;break
   if not termios.tcgetattr(master)[3]&termios.ICANON:
    os.write(master,b'RBH1T\0\0\0')
    if select.select([master],[],[],1)[0]:received=os.read(master,8)==b'RBS1T\0\0\0'
    os.kill(pid,signal.SIGTERM);os.waitpid(pid,0);pid=0;break
   time.sleep(.01)
  assert not pid and received==(node is None)
  assert termios.tcgetattr(master)==saved
  print('PASS',node.name if node else 'unaliased PTY positive control')
 finally:
  if pid:os.kill(pid,signal.SIGKILL);os.waitpid(pid,0)
  os.close(master)
  if node is not None:node.unlink()
print('TOTAL 9 PASS; synthetic PTY aliases only')
