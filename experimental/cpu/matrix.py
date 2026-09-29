"""Run configurations in interleaved order (ABCD DCBA ...) and summarize.

Each configuration is a list of session.py arguments. Results are appended to
out/cpu-work/matrix-<name>.jsonl so an interrupted matrix keeps what it has.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
CONFIGS = {
    # Cached buffer memory and the native kernels are on by default; the
    # baseline turns both off.
    'baseline': ['--setprop', 'debug.axrb.guest_accel=0', '--setprop', 'debug.axrb.cached_buffer_memory=0', '--transport', 'Pipe'],
    'asg': ['--setprop', 'debug.axrb.guest_accel=0', '--avd-config', 'hw.gltransport=asg'],
    'cached': ['--setprop', 'debug.axrb.guest_accel=0', '--setprop', 'debug.axrb.cached_buffer_memory=1'],
    'accel': [],
    'accel-cached': ['--setprop', 'debug.axrb.cached_buffer_memory=1'],
    'asg-cached': ['--setprop', 'debug.axrb.guest_accel=0', '--setprop', 'debug.axrb.cached_buffer_memory=1',
                   '--avd-config', 'hw.gltransport=asg'],
    'cached-only': ['--setprop', 'debug.axrb.guest_accel=0', '--setprop', 'debug.axrb.cached_buffer_memory=1'],
    'cached-jw0': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args', '-job-worker-count 0'],
    'cached-jw1': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args', '-job-worker-count 1'],
    'jw0-3cpu': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args', '-job-worker-count 0', '--cores', '3'],
    'jw0-2cpu': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args', '-job-worker-count 0', '--cores', '2'],
    'jw0-direct': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args', '-job-worker-count 0 -force-gfx-direct'],
    'jw0-direct-2cpu': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args',
                        '-job-worker-count 0 -force-gfx-direct', '--cores', '2'],
    'jw1-direct': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--unity-args', '-job-worker-count 1 -force-gfx-direct'],
    # Stock translator (Berberis) options: region translation and eager heavy optimization.
    'jwd-regions': ['--unity-args', '-job-worker-count 0 -force-gfx-direct',
                    '--build-prop', 'ro.berberis.flags=accurate-sigsegv,enable-disjoint-regions-translation'],
    'jwd-regions-merge': ['--unity-args', '-job-worker-count 0 -force-gfx-direct', '--build-prop',
                          'ro.berberis.flags=accurate-sigsegv,enable-disjoint-regions-translation,merge-profiles-for-same-mode-regions'],
    'jwd-heavy': ['--unity-args', '-job-worker-count 0 -force-gfx-direct', '--setprop', 'berberis.mode=heavy-optimize'],
    'jwd-nosigsegv': ['--unity-args', '-job-worker-count 0 -force-gfx-direct', '--build-prop', 'ro.berberis.flags=none'],
    'direct': ['--unity-args', '-force-gfx-direct'],
    'regions': ['--build-prop', 'ro.berberis.flags=accurate-sigsegv,enable-disjoint-regions-translation'],
    'heavy': ['--setprop', 'berberis.mode=heavy-optimize'],
    'nosigsegv': ['--build-prop', 'ro.berberis.flags=none'],
    'nohz': ['--avd-config', 'kernel.parameters=nohz=off'],
    'default': ['--transport', 'Pipe'],
    'product': ['--hv-apic'],  # hypervisor APIC and asg, the defaults since Finding 7
    'product-nowake': ['--hv-apic', '--hv-no-wake'],
    'product-thp': ['--hv-apic', '--guest-shell', 'echo always > /sys/kernel/mm/transparent_hugepage/enabled; echo defer > /sys/kernel/mm/transparent_hugepage/defrag'],
    'hvapic': ['--hv-apic', '--transport', 'Pipe'],
    'hvapic-jw0': ['--hv-apic', '--unity-args', '-job-worker-count 0'],
    'hvapic-direct': ['--hv-apic', '--unity-args=-force-gfx-direct'],
    'hvapic-jwd': ['--hv-apic', '--unity-args', '-job-worker-count 0 -force-gfx-direct'],
    'hvapic-asg': ['--hv-apic', '--transport', 'Asg'],
    'hvapic-3cpu': ['--hv-apic', '--cores', '3'],
    'hvapic-heavy': ['--hv-apic', '--setprop', 'berberis.mode=heavy-optimize'],
    'hvapic-nosigsegv': ['--hv-apic', '--build-prop', 'ro.berberis.flags=none'],
    'hvapic-regions': ['--hv-apic', '--build-prop', 'ro.berberis.flags=accurate-sigsegv,enable-disjoint-regions-translation'],
    'x2apic': ['--hv-x2apic'],
    'jwd-x2apic': ['--hv-x2apic', '--unity-args', '-job-worker-count 0 -force-gfx-direct'],
    'jwd-hvapic': ['--hv-apic', '--unity-args', '-job-worker-count 0 -force-gfx-direct'],
    'full': ['--setprop', 'debug.axrb.cached_buffer_memory=1', '--avd-config', 'hw.gltransport=asg'],
}


def summarize(path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    by = {}
    for row in rows:
        for window in row['windows']:
            by.setdefault(row['config'], []).append(window)
    print(f'{"config":14} {"n":>2} {"host cores":>11} {"guest busy":>10} {"game cores":>10} {"fps":>6} {"host ms/f":>9} {"game ms/f":>9}')
    for name, windows in by.items():
        def med(key):
            values = [w[key] for w in windows if w.get(key) is not None]
            return statistics.median(values) if values else float('nan')
        print(f'{name:14} {len(windows):>2} {med("host_qemu_cores"):>11.3f} {med("guest_busy_cores"):>10.3f} '
              f'{med("guest_game_cores"):>10.3f} {med("fresh_fps"):>6.1f} {med("host_ms_per_fresh_frame"):>9.2f} '
              f'{med("game_ms_per_fresh_frame"):>9.2f}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--name', required=True)
    p.add_argument('--configs', nargs='+', default=['baseline', 'full'])
    p.add_argument('--rounds', type=int, default=2)
    p.add_argument('--game', default='nexus')
    p.add_argument('--extra', nargs=argparse.REMAINDER, default=[], help='arguments for every session')
    p.add_argument('--summarize', action='store_true')
    a = p.parse_args()
    path = ROOT / 'out/cpu-work' / f'matrix-{a.name}.jsonl'
    if a.summarize:
        summarize(path); return
    for round_index in range(a.rounds):
        order = a.configs if round_index % 2 == 0 else list(reversed(a.configs))
        for name in order:
            label = f'{a.name}-{name}-r{round_index}'
            command = [sys.executable, str(ROOT / 'experimental/cpu/session.py'), '--game', a.game, '--label', label,
                       '--seconds', '20', '--warmup', '90', '--repeat', '2', *CONFIGS[name], *a.extra]
            print(time.strftime('%H:%M:%S'), 'run', label, flush=True)
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            saved = [line for line in result.stdout.splitlines() if line.startswith('saved ')]
            if result.returncode or not saved:
                print('FAILED', label, result.stdout[-1500:], result.stderr[-1500:], flush=True)
                subprocess.run([sys.executable, str(ROOT / 'experimental/cpu/kill_run.py')])
                time.sleep(5)
                continue
            data = json.loads((Path(saved[-1][6:]) / 'result.json').read_text())
            data['config'] = name
            data['round'] = round_index
            with path.open('a') as f:
                f.write(json.dumps(data) + '\n')
            for w in data['windows']:
                print(f'   host={w["host_qemu_cores"]} game={w["guest_game_cores"]} fps={w["fresh_fps"]} '
                      f'host_ms/f={w["host_ms_per_fresh_frame"]}', flush=True)
            time.sleep(3)
    summarize(path)


if __name__ == '__main__':
    main()
