"""Real controlling-tty alias fixture in disposable Linux PTYs, not target console."""
import fcntl,os,pty,signal,struct,termios
from pathlib import Path
assert Path('/.dockerenv').exists()
a,sa=pty.openpty();b,sb=pty.openpty()
wa=struct.pack('HHHH',24,80,0,0);wb=struct.pack('HHHH',40,90,0,0)
fcntl.ioctl(a,termios.TIOCSWINSZ,wa);fcntl.ioctl(b,termios.TIOCSWINSZ,wb)
pid=os.fork()
if not pid:
 try:
  os.setsid();signal.signal(signal.SIGHUP,signal.SIG_IGN)
  fcntl.ioctl(sa,termios.TIOCSCTTY,0)
  old=os.open('/dev/tty',os.O_RDWR)
  assert fcntl.ioctl(old,termios.TIOCGWINSZ,bytes(8))==wa
  fcntl.ioctl(sa,termios.TIOCNOTTY,0)
  fcntl.ioctl(sb,termios.TIOCSCTTY,0)
  new=os.open('/dev/tty',os.O_RDWR)
  assert os.fstat(old).st_rdev==os.fstat(new).st_rdev==os.makedev(5,0)
  assert fcntl.ioctl(old,termios.TIOCGWINSZ,bytes(8))==wa
  assert fcntl.ioctl(new,termios.TIOCGWINSZ,bytes(8))==wb
  statline=Path('/proc/self/stat').read_text().rsplit(')',1)[1].split()
  assert int(statline[4])==os.fstat(sb).st_rdev
  # Opening the proc fd link is a new alias open in this process's current tty.
  reopened=os.open('/proc/self/fd/'+str(old),os.O_RDWR)
  assert fcntl.ioctl(reopened,termios.TIOCGWINSZ,bytes(8))==wb
  os._exit(0)
 except BaseException:
  import traceback;traceback.print_exc();os._exit(1)
_,status=os.waitpid(pid,0)
for fd in [a,sa,b,sb]:os.close(fd)
assert os.waitstatus_to_exitcode(status)==0
print('PASS old and new dev tty aliases retain different tty objects')
print('PASS current tty_nr does not identify old dev tty file binding')
print('PASS proc fd reopen resolves alias anew')
print('TOTAL 3 PASS; actual Linux PTYs, not vendor runtime or physical serial')
