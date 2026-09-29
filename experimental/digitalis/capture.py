"""Measure an already-running stationary Nexus scene without restarting it."""
import argparse
import json
from pathlib import Path
import re
import time

p=argparse.ArgumentParser()
p.add_argument('--out',required=True)
p.add_argument('--seconds',type=int,default=60)
a=p.parse_args()
if not 10<=a.seconds<=300: p.error('seconds must be between 10 and 300')
root=Path(__file__).resolve().parents[2]
logs=root/'out/digitalis-test/logs/game'
out=Path(a.out);out.mkdir(parents=True,exist_ok=False)
streams={name:(logs/name).open('r',encoding='utf-8',errors='replace') for name in ['host.err','guest.log']}
chunks={name:[] for name in streams}
prior_states=re.findall(r'AXRB OpenXR: session state=(\d+)',streams['host.err'].read())
initial_state=int(prior_states[-1]) if prior_states else None
for stream in streams.values():stream.seek(0,2)
try:
    deadline=time.monotonic()+a.seconds
    while time.monotonic()<deadline:
        time.sleep(min(1,max(0,deadline-time.monotonic())))
        for name,stream in streams.items():chunks[name].append(stream.read())
finally:
    for stream in streams.values():stream.close()
for name,parts in chunks.items():(out/name).write_text(''.join(parts),encoding='utf-8')
samples=[]
for line in ''.join(chunks['host.err']).splitlines():
    if 'AXRB TruePerf:' in line:
        samples.append({k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)})
reasons=[]
if len(samples)<5:reasons.append('too few telemetry samples')
states=[initial_state]+[int(s) for s in re.findall(r'AXRB OpenXR: session state=(\d+)',''.join(chunks['host.err']))]
# A slow host can be the regression under test. Never discard it as standby
# based on FPS alone; OpenXR supplies explicit visibility transitions.
if any(s not in (4,5) for s in states):reasons.append('OpenXR session not continuously visible')
if len({(s.get('width'),s.get('height')) for s in samples})>1:reasons.append('resolution changed')
if any(b['fresh_total']<a['fresh_total'] for a,b in zip(samples,samples[1:])):reasons.append('session restarted')
result={'invalid_reasons':reasons,'session_states':states,
        'low_host_rate_observed':any(s.get('host_fps',0)<20 for s in samples),'samples':samples}
if len(samples)>1:
    first,last=samples[0],samples[-1]
    elapsed=(last['steady_ns']-first['steady_ns'])/1e9
    if elapsed>0:
        result.update(seconds=elapsed,fresh_fps=(last['fresh_total']-first['fresh_total'])/elapsed,
            host_fps=(last['host_total']-first['host_total'])/elapsed,
            worst_observed_gap_ms=max(s['worst_ms'] for s in samples),
            resolution=[last.get('width'),last.get('height')])
(out/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
