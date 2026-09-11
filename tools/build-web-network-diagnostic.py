"""Build read-only probe and separate corrected Gateway; no ARM execution."""
from pathlib import Path
import json,os,re,shutil,subprocess,sys
host,previous,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
source=out/'source'
for directory in ['src','tools','tests']:
    shutil.copytree(host/directory,source/directory,dirs_exist_ok=True)
def run(command,log=None,env=None):
    r=subprocess.run(list(map(str,command)),capture_output=True,env=env)
    if log:(out/log).write_bytes(r.stdout+r.stderr)
    if r.returncode:print((r.stdout+r.stderr).decode(errors='replace'));raise SystemExit(r.returncode)
    return r
# Old target binutils can retain dynamic imports from discarded service code.
# Compile exact production query functions only, with no guardian/start paths.
service=(source/'src/network/gateway_network_service.c').read_text()
query=service[:service.index('typedef struct mutation')]
for name in ['path','nonblock','connect_socket','gateway_network_service_command','gateway_network_service_read','gateway_network_service_query']:
    match=re.search(r'(?m)^(?:static )?int '+name+r'\(',service);assert match,name
    start=service.index('{',match.start());depth=1;end=start+1
    while depth:
        depth+=(service[end]=='{')-(service[end]=='}');end+=1
    query+=service[match.start():end]+'\n'
(source/'probe-query-only.c').write_text(query)
network=['probe-query-only.c','src/network/gateway_network_observation.c','src/network/gateway_network_settings.c']
for mode in ['host','ubsan','target']:
    cc='/usr/local/xscale_be/bin/xscale_be-gcc' if mode=='target' else 'cc'
    flags=['-std=c99','-Os','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-I'+str(source/'src')]
    if mode=='target':flags+=['-mcpu=xscale','-mbig-endian','-msoft-float']
    if mode=='ubsan':flags+=['-fsanitize=undefined','-fno-sanitize-recover=all']
    extra=[]
    if mode!='target':extra=[source/'tests/network/test_web_network_probe_provider.c','-Wl,--wrap=socket,--wrap=ioctl,--wrap=close,--wrap=gateway_network_service_query,--wrap=gateway_network_observe']
    binary=out/('4vrs-web-network-probe' if mode=='target' else 'probe-fixture-'+mode)
    run([cc,*flags,source/'tools/web-network-readonly-probe.c',*[source/n for n in network],*extra,'-Wl,--gc-sections','-lrt','-o',binary],mode+'-probe-build.log')
    if mode!='target':
        for case,code,expected in [('healthy',0,'owner.observed.eth1 present=1 up=1 link=0'),('owner-error',0,'error=7'),('unsupported',0,'direct.unsupported=2'),('query-error',2,'query.result=-1'),('direct-error',2,'direct.result=-1'),('wrong-mac',65,'identity mismatch')]:
            r=subprocess.run([binary,'--moxa1-readonly'],capture_output=True,env={**os.environ,'PROBE_CASE':case});(out/(mode+'-probe-'+case+'.log')).write_bytes(r.stdout+r.stderr)
            assert r.returncode==code and expected.encode() in r.stdout+r.stderr,(mode,case,r.returncode)
            if case=='wrong-mac':assert b'query.result' not in r.stdout
        print(mode+': six probe fixtures PASS',flush=True)
gateway=re.findall(r'"\$root/([^"\n]+\.c)"',(source/'tools/build-gateway-application.sh').read_text())
gateway+=['src/web/web_protocol.c','src/web/rng_client.c','src/web/web_gateway.c','src/web/web_ipc_endpoint.c','src/web/web_security.c']
tls=previous/'dependency/mbedtls-3.6.7'
run(['/usr/local/xscale_be/bin/xscale_be-gcc','-std=c99','-Os','-Wall','-Wextra','-mcpu=xscale','-mbig-endian','-msoft-float',
     '-DFOURVRS_WITH_WEB','-DFOURVRS_VERSION="v2026.02.01"','-DMBEDTLS_CONFIG_FILE="web/mbedtls_config.h"',
     '-I'+str(source/'src'),'-I'+str(tls/'include'),*[source/n for n in gateway],previous/'target/tls/libtls.a','-lrt','-o',out/'4vrs-gateway-observer-fix'], 'target-gateway-build.log')
print('Built separate XScale probe and corrected Gateway; no deployment',flush=True)
