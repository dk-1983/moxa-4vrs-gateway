"""Offline summary regression: real percentiles, failed SSH visibility, no network."""
import json, subprocess, sys, tempfile
from pathlib import Path
root = Path(__file__).resolve().parents[2]
rows = [{'event': 'modbus', 'phase': 'baseline', 'seconds': i, 'error': None} for i in range(1, 101)]
rows += [{'event':'modbus','phase':'baseline','seconds':999,'error':'timeout'},
         {'event':'resources','phase':'baseline','exit':255,'stat':'','utc':'test'},
         {'event':'web','phase':'mixed','seconds':.2,'status':200,'new_tls':True},
         {'event':'web_error','phase':'mixed','seconds':8,'error':'timeout'},
         {'event':'operator_action','action':'vpn-change','utc':'test'}]
with tempfile.TemporaryDirectory() as d:
    inp, out = Path(d)/'input.jsonl', Path(d)/'summary.json'
    inp.write_text('\n'.join(json.dumps(r) for r in rows), encoding='utf8')
    subprocess.run([sys.executable, str(root/'tools/summarize-web-autonomous.py'), str(inp), str(out)], check=True)
    r = json.loads(out.read_text())
    assert r['phases']['baseline']['transactions'] == 101
    assert r['phases']['baseline']['errors'] == 1
    assert [r['phases']['baseline'][k] for k in ['p50_s','p95_s','p99_s']] == [50,95,99]
    assert r['phases']['mixed']['web']['new_tls'] == 1
    assert r['phases']['mixed']['web']['errors'] == 1
    assert r['phases']['user-only']['p99_s'] is None
    assert len(r['availability_errors']) == 3 and len(r['operator_actions']) == 1
print('Offline summary percentiles/web/SSH errors/empty phase PASS')
