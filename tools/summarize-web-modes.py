"""Offline summary of Web-only JSONL. Never treats a phase label as active load."""
import argparse, collections, json, math
from pathlib import Path

def summarize(rows):
    groups = {}
    for r in rows:
        if r.get('event') not in ('request', 'web_error'): continue
        g = groups.setdefault(r.get('scenario', 'unknown'), {'completed': 0, 'statuses': {}, 'errors_by_stage': {}, 'latencies_s': []})
        if r['event'] == 'request':
            g['completed'] += 1
            key = str(r['status']); g['statuses'][key] = g['statuses'].get(key, 0) + 1
            g['latencies_s'].append(r['complete_s'])
        else:
            key = r['failed_stage'] + ':' + r['error']; g['errors_by_stage'][key] = g['errors_by_stage'].get(key, 0) + 1
    for g in groups.values():
        values = sorted(g.pop('latencies_s'))
        g['latency_s'] = {str(p): values[max(0, math.ceil(len(values)*p/100)-1)] if values else None for p in (50, 95, 99)}
    return {'scenarios': groups,
            'events': dict(collections.Counter(r.get('event') for r in rows)),
            'load_boundaries': [r for r in rows if r.get('event') in ('load_started', 'load_stopped', 'synthetic_complete')],
            'operator_actions': [r for r in rows if r.get('event') == 'operator_action'],
            'complete': bool(rows and rows[-1].get('event') == 'end' and not any(r.get('event') == 'abort' for r in rows)),
            'limitations': 'Successful-request percentiles exclude failed requests: errors are counted separately. No CPU inference from phase labels. Inspect resource snapshots and actual load boundaries; SSH overhead and unattributed short-lived KDF work remain. No Modbus claim without a separately approved profile.'}

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('log', type=Path); p.add_argument('--output', type=Path)
    a = p.parse_args(); result = json.dumps(summarize([json.loads(s) for s in a.log.read_text(encoding='utf8').splitlines() if s.strip()]), indent=2)
    if a.output: a.output.write_text(result+'\n', encoding='utf8')
    else: print(result)
