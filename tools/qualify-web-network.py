"""Focused local observer regression build. No target access."""
from pathlib import Path
import re,subprocess,sys
root,out=map(Path,sys.argv[1:3]);mode=sys.argv[3];out.mkdir(parents=True,exist_ok=True)
sources=re.findall(r'"\$root/([^"\n]+\.c)"',(root/'tools/build-gateway-application.sh').read_text())
network=[s for s in sources if s.startswith('src/network/gateway_') and s!='src/network/gateway_network_runtime.c']
flags=['-std=c99','-O2','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-I'+str(root/'src')]
if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
command=['cc',*flags,str(root/'tests/network/test_gateway_observer_lifecycle.c'),*[str(root/s) for s in network],str(root/'src/core/deadline.c'),'-Wl,--gc-sections,--wrap=read,--wrap=waitpid,--wrap=kill,--wrap=close','-lrt','-o',str(out/'observer-test')]
subprocess.run(command,check=True)
r=subprocess.run([out/'observer-test'],capture_output=True);(out/'observer.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode())
if r.returncode:sys.exit(r.returncode)
subprocess.run(['cc',*flags,'-Wno-misleading-indentation',str(root/'tests/web/test_web_network_gate.c'),str(root/'src/web/web_gateway.c'),'-Wl,--gc-sections','-lrt','-o',str(out/'web-gate-test')],check=True)
r=subprocess.run([out/'web-gate-test'],capture_output=True);(out/'web-gate.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode())
if r.returncode:sys.exit(r.returncode)
subprocess.run(['cc',*flags,str(root/'tests/panel/test_web_error_labels.c'),'-Wl,--gc-sections','-o',str(out/'lcd-test')],check=True)
r=subprocess.run([out/'lcd-test'],capture_output=True);(out/'lcd.log').write_bytes(r.stdout+r.stderr);print((r.stdout+r.stderr).decode());sys.exit(r.returncode)
