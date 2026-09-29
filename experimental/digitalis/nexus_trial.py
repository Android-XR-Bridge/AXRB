"""Capture a bounded startup/menu trial. Standby results are explicitly labelled."""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

p=argparse.ArgumentParser()
p.add_argument('--out',required=True)
p.add_argument('--seconds',type=int,default=30)
p.add_argument('--warmup',type=int,default=15)
p.add_argument('--quiet',action='store_true')
p.add_argument('--no-ir-validation',action='store_true')
p.add_argument('--cores',type=int,default=4)
a=p.parse_args()
root=Path(__file__).resolve().parents[2]
out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=False)
logs=root/'out/digitalis-test/logs/game'
args=['powershell','-ExecutionPolicy','Bypass','-File',str(root/'experimental/digitalis/test.ps1'),'-Action','Run','-CpuCores',str(a.cores)]
if a.quiet:args.append('-QuietTranslator')
if a.no_ir_validation:args.append('-DisableIrValidation')
def read(path):
    try:return path.read_text(encoding='utf-8',errors='replace')
    except (FileNotFoundError,PermissionError):return ''
start=time.monotonic()
host_pid=None
with (out/'launch.log').open('w') as output:
    process=subprocess.Popen(args,cwd=root,stdout=output,stderr=subprocess.STDOUT)
    try:
        while time.monotonic()-start<120:
            launch=read(out/'launch.log')
            match=re.search(r'SteamVR identity: .*?"pid":\s*(\d+)',launch)
            if match:
                host_pid=int(match[1])
                if re.search(r'fresh_total=[1-9]',read(logs/'host.err')):break
            if process.poll() is not None:raise RuntimeError(launch)
            time.sleep(1)
        else:raise RuntimeError('No fresh frames within 120 seconds')
        first_frame=time.monotonic()-start
        time.sleep(a.warmup)
        host_offset=len(read(logs/'host.err'))
        guest_offset=len(read(logs/'guest.log'))
        time.sleep(a.seconds)
        host=read(logs/'host.err')[host_offset:]
        guest=read(logs/'guest.log')[guest_offset:]
        (out/'host-window.log').write_text(host,encoding='utf-8')
        (out/'guest-window.log').write_text(guest,encoding='utf-8')
        perf=[]
        for line in host.splitlines():
            if 'AXRB TruePerf:' in line:
                perf.append({k:float(v) for k,v in re.findall(r'(\w+)=(-?[\d.]+)',line)})
        result={'first_frame_seconds':first_frame,'samples':perf,
                'standby_detected':any(r.get('host_fps',0)<20 for r in perf),
                'options':vars(a)}
        (out/'result.json').write_text(json.dumps(result,indent=2))
        print(json.dumps({k:v for k,v in result.items() if k!='samples'}),flush=True)
    finally:
        if host_pid:
            subprocess.run(['powershell','-NoProfile','-Command',f'$p=Get-Process -Id {host_pid} -ErrorAction SilentlyContinue; if($p){{[void]$p.CloseMainWindow()}}'],timeout=10)
        try:process.wait(timeout=20)
        except subprocess.TimeoutExpired:
            process.terminate()
            raise RuntimeError('Session cleanup timed out; check emulator before next trial')
        for name in ['host.err','guest.log','guest-cpu.log']:
            if (logs/name).exists():shutil.copyfile(logs/name,out/name)
