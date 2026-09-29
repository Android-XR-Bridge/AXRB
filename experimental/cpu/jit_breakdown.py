"""Attribute translated-code samples per thread to guest libraries and functions.

Needs a session.py --simpleperf run on Digitalis with berberis.profiling set
(um_perf.map). Uses the NDK's host simpleperf to read perf.data.
"""
import argparse
import bisect
import collections
import os
from pathlib import Path
import re
import subprocess

SIMPLEPERF = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk/ndk/29.0.14206865/simpleperf/bin/windows/x86_64/simpleperf.exe'


def report(profile, *args):
    return subprocess.run([str(SIMPLEPERF), 'report', '-i', str(profile / 'perf.data'), *args],
                          capture_output=True, text=True).stdout


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('profile', type=Path)
    p.add_argument('--top', type=int, default=15)
    a = p.parse_args()
    regions = []
    pattern = re.compile(r'^(.*?)_(lite|heavy|[a-z]+)_0x([0-9a-f]+)\+(\d+)')
    for order, line in enumerate((a.profile / 'um_perf.map').read_text().splitlines()):
        fields = line.split(None, 2)
        if len(fields) == 3 and (m := pattern.match(fields[2])):
            begin = int(fields[0], 16)
            regions.append((begin, begin + int(fields[1], 16), order, m.group(1), int(m.group(3), 16)))
    regions.sort()
    starts = [r[0] for r in regions]

    def region(ip):
        i = bisect.bisect_right(starts, ip) - 1
        best = None
        for j in range(i, max(i - 64, -1), -1):
            r = regions[j]
            if r[0] <= ip < r[1] and (best is None or r[2] > best[2]):
                best = r
        return best

    # Library load bases, to print guest offsets usable with objdump.
    bases = {}
    for line in (a.profile / 'um_maps.txt').read_text().splitlines():
        parts = line.split()
        if len(parts) >= 6 and parts[2] == '00000000':
            bases.setdefault(parts[5].rsplit('/', 1)[-1][-16:], []).append(int(parts[0].split('-')[0], 16))

    threads = report(a.profile, '--sort', 'tid,comm', '-n', '--percent-limit', '1')
    total = int(re.search(r'Samples: (\d+)', threads).group(1))
    for line in threads.splitlines():
        m = re.match(r'\s*([\d.]+)%\s+(\d+)\s+(\d+)\s+(.+)$', line)
        if not m:
            continue
        tid, name, samples = m.group(3), m.group(4).strip(), int(m.group(2))
        dsos = report(a.profile, '--tids', tid, '--sort', 'dso', '-n', '--percent-limit', '2')
        print(f'\n== {name} (tid {tid}) {samples / total * 100:.1f}% of process samples')
        for dline in dsos.splitlines():
            dm = re.match(r'\s*([\d.]+)%\s+(\d+)\s+(.+)$', dline)
            if dm:
                print(f'   dso {dm.group(3).strip():50} {dm.group(1)}%')
        ips = report(a.profile, '--tids', tid, '--dsos', 'unknown', '--sort', 'vaddr_in_file', '-n')
        libs, funcs = collections.Counter(), collections.Counter()
        for iline in ips.splitlines():
            im = re.match(r'\s*[\d.]+%\s+(\d+)\s+0x([0-9a-f]+)', iline)
            if not im:
                continue
            count, ip = int(im.group(1)), int(im.group(2), 16)
            r = region(ip)
            lib = r[3] if r else '(unmapped)'
            libs[lib] += count
            if r:
                base = min((b for b in bases.get(lib, []) if b <= r[4]), default=None, key=lambda b: r[4] - b)
                funcs[(lib, hex(r[4] - base) if base else hex(r[4]))] += count
        jit = sum(libs.values())
        if not jit:
            continue
        print(f'   translated code: {jit / samples * 100:.0f}% of thread; by library:',
              ', '.join(f'{k} {v / jit * 100:.0f}%' for k, v in libs.most_common(8)))
        for (lib, offset), count in funcs.most_common(a.top):
            print(f'      {count / jit * 100:5.1f}%  {lib} +{offset}')


if __name__ == '__main__':
    main()
