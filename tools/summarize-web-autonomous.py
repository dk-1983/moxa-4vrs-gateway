"""Nonsecret offline latency/error summary; no inference of UART from TCP connect."""
import json,sys,statistics,math
from pathlib import Path
rows=[json.loads(x) for x in Path(sys.argv[1]).read_text(encoding='utf8').splitlines()]
def percentile(values,q):
 return values[max(0,math.ceil(len(values)*q)-1)] if values else None
out={'phases':{},'operator_actions':[r for r in rows if r['event']=='operator_action'],'availability_errors':[]}
for phase in ['baseline','user-only','mixed']:
 values=[r for r in rows if r.get('phase')==phase and r['event']=='modbus'];latencies=sorted(r['seconds'] for r in values if not r['error'])
 web=[r for r in rows if r.get('phase')==phase and r['event']=='web'];wl=sorted(r['seconds'] for r in web)
 out['phases'][phase]={'transactions':len(values),'errors':sum(bool(r['error']) for r in values),'p50_s':percentile(latencies,.5),'p95_s':percentile(latencies,.95),'p99_s':percentile(latencies,.99),'web':{'responses':len(web),'new_tls':sum(r['new_tls'] for r in web),'errors':sum(r.get('phase')==phase and r['event']=='web_error' for r in rows),'p50_s':percentile(wl,.5),'p95_s':percentile(wl,.95),'p99_s':percentile(wl,.99)},'resource_samples':[r for r in rows if r.get('phase')==phase and r['event']=='resources']}
for r in rows:
 if r['event'] in ['collector_error','web_error'] or r['event']=='modbus' and r.get('error') or r['event']=='resources' and r.get('exit'):
  out['availability_errors'].append({k:r.get(k) for k in ['utc','event','phase','error','exit']})
out['interpretation']='VPN/route markers and availability errors are separate observations, not proven causes. Resource stat snapshots include collector overhead; reaped child CPU may include more than KDF. Never infer serial exchange from TCP connect.'
Path(sys.argv[2]).write_text(json.dumps(out,indent=2)+'\n')
