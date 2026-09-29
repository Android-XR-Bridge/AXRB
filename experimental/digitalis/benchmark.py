"""Run alternating APK CPU probes on the isolated Digitalis AVD; restore properties."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--adb', required=True)
p.add_argument('--out', required=True)
p.add_argument('--rounds', type=int, default=4)
a = p.parse_args()
out = Path(a.out)
out.mkdir(parents=True, exist_ok=False)

def adb(*args):
    return subprocess.check_output([a.adb, '-P', '5038', '-s', 'emulator-5586', *args],
                                   text=True, encoding='utf-8', errors='replace', timeout=30).strip()

if 'axrb-digitalis-test' not in adb('emu', 'avd', 'name').splitlines():
    raise SystemExit('Isolated test AVD required')
if adb('shell', 'getprop', 'ro.dalvik.vm.native.bridge') != 'libberberis_arm64.so':
    raise SystemExit('Digitalis required')
if adb('shell', 'sh', '-c', "'pidof com.Ubisoft.ACNexusVR || true'"):
    raise SystemExit('Stop Nexus before the isolated CPU benchmark')
properties = ['berberis.mode', 'berberis.flags', 'log.tag.berberis']
original = {key: adb('shell', 'getprop', key) for key in properties}
base_flags = original['berberis.flags'] or adb('shell', 'getprop', 'ro.berberis.flags')
variants = {
    'baseline': ('two-gear', base_flags, original['log.tag.berberis']),
    'lite': ('lite-translate-or-interpret', base_flags, original['log.tag.berberis']),
    'no_ir_validation': ('two-gear', ','.join(filter(None, [base_flags, 'disable-ir-check'])), original['log.tag.berberis']),
    'quiet': ('two-gear', base_flags, 'W'),
}
results = []
try:
    for repeat in range(a.rounds):
        order = list(variants)
        if repeat % 2: order.reverse()
        for name in order:
            for key, value in zip(properties, variants[name]):
                adb('shell', f"setprop {key} '{value}'")
            adb('shell', 'am', 'force-stop', 'com.axrb.dynarmicprobe')
            started = time.perf_counter()
            adb('shell', 'am', 'start', '-W', '-n', 'com.axrb.dynarmicprobe/.MainActivity', '--ez', 'benchmark', 'true')
            pid = adb('shell', 'pidof', 'com.axrb.dynarmicprobe')
            deadline = time.monotonic() + 20
            while True:
                log = adb('logcat', '-d', f'--pid={pid}', '-s', 'AXRB.CpuProbe:I', 'AXRB.DynarmicProbe:I', '*:S')
                if 'BENCHMARK_COMPLETE' in log: break
                if 'BENCHMARK_FAILED' in log or time.monotonic() > deadline:
                    raise RuntimeError(f'{name} probe failed: {log}')
                time.sleep(.1)
            (out / f'{repeat}-{name}.log').write_text(log, encoding='utf-8')
            samples = re.findall(r'TIME (\w+) (\d+) ([\d.]+)', log)
            if len(samples) != 24 or 'CHECK memory PASS' not in log:
                raise RuntimeError('Missing probe results')
            result = {'variant': name, 'repeat': repeat,
                      'launch_wall_ms': (time.perf_counter()-started)*1000,
                      'clrex_pass': 'CHECK clrex PASS' in log, 'timings': {}}
            for kind in ['integer', 'simd', 'atomic']:
                values = [float(ms) for k, _, ms in samples if k == kind]
                result['timings'][kind] = {'cold_ms': values[0], 'warm_median_ms': statistics.median(values[1:])}
            results.append(result)
            print(json.dumps(result), flush=True)
finally:
    for key, value in original.items():
        adb('shell', f"setprop {key} '{value}'")
    (out / 'results.json').write_text(json.dumps({'original_properties': original,
        'variants': variants, 'results': results}, indent=2), encoding='utf-8')
