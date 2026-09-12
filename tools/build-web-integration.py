"""Build actual Web server and Gateway with its broker; no target execution."""
import re,subprocess,sys
from pathlib import Path
root,tls,out=map(Path,sys.argv[1:4]);mode=sys.argv[4];out.mkdir(parents=True,exist_ok=True)
assets=Path(sys.argv[5])
def run(args):subprocess.run(list(map(str,args)),check=True)
run(['python3',root/'tools/embed-web-assets.py',root,assets,out])
run(['sh',root/'tools/build-web-tls.sh',root,tls,out/'tls',mode])
cc='/usr/local/mxscaleb/bin/mxscaleb-gcc' if mode=='linux24' else '/usr/local/xscale_be/bin/xscale_be-gcc' if mode in ('target','linux24') else 'cc'
flags=['-std=c99','-Os','-Wall','-Wextra','-DMBEDTLS_CONFIG_FILE="web/mbedtls_config.h"','-I'+str(root/'src'),'-I'+str(root/'tests'),'-I'+str(tls/'include')]
# Product version comes from src/version.h.
if mode=='linux24':flags=[('-W' if f=='-Wextra' else f) for f in flags]+['-DFOURVRS_LINUX24']
if mode in ('target','linux24'):flags+=['-mcpu=xscale','-mbig-endian','-msoft-float']
else:flags+=['-Wno-misleading-indentation','-DWEB_HOST_TEST']
if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all','-g']
common=['src/web/web_protocol.c','src/web/rng_client.c']
run(['python3',root/'tools/build-rng.py',root,tls,out,mode])
run([cc,*flags,*(['-Wl,--wrap=fsync'] if mode not in ('target','linux24') else []),root/'src/web/kdf_main.c',root/'src/web/web_protocol.c',root/'src/web/rng_client.c',out/'tls/libtls.a','-lrt','-o',out/'4vrs-kdf'])
server=['src/web/web_server.c','src/web/web_http.c','src/web/web_certificate.c']+common
run([cc,*flags,*[root/s for s in server],out/'web-assets.c',out/'tls/libtls.a','-lrt','-o',out/'4vrs-web'])
gateway=re.findall(r'"\$root/([^"\n]+\.c)"',(root/'tools/build-gateway-application.sh').read_text())
gateway+=common+['src/web/web_gateway.c','src/web/web_ipc_endpoint.c','src/web/web_security.c']
run([cc,*flags,'-DFOURVRS_WITH_WEB',*[root/s for s in gateway],out/'tls/libtls.a','-lrt','-o',out/'4vrs-gateway'])
run([cc,*flags,root/'src/web/web_cost_probe.c',out/'tls/libtls.a','-lrt','-o',out/'4vrs-web-cost-probe'])
if mode not in ('target','linux24'):
 sources=[s for s in gateway if s!='src/app/main.c']+['tests/web/web_integration_host.c','tests/gateway/gateway_orchestration_mock.c','src/core/mock_backend.c']
 run([cc,*flags,'-Wno-unused-function','-Wl,--wrap=fsync,--wrap=recv',*[root/s for s in sources],out/'tls/libtls.a','-lrt','-o',out/'gateway-host'])
 run([cc,*flags,'-Wl,--wrap=web_random,--wrap=time,--wrap=fsync',root/'tests/web/test_web_core.c',root/'src/web/web_protocol.c',root/'src/web/rng_client.c',root/'src/web/web_security.c',root/'src/web/web_http.c',root/'src/web/web_certificate.c',out/'tls/libtls.a','-lrt','-o',out/'test-web-core'])

if mode not in ('target','linux24'):
 run([cc,*flags,root/'tests/web/test_rng_client.c',root/'src/web/rng_client.c',root/'src/web/web_protocol.c','-lrt','-o',out/'test-rng-client'])
