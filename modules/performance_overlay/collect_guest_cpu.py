"""Read-only, 2-second Android /proc sampler and bounded historical telemetry.

One adb shell per sample; bulk native cat reads avoid slow shell per-character
loops and spawning a process per thread. Atomic 3-line HUD snapshot; optional
bounded resource JSONL with Windows/GPU data. Percentages use guest scheduler
ticks, NOT host total CPU or virtual wall time.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import subprocess
import time
import shutil

COMMAND = r'''pid=$(pidof PACKAGE_NAME); set -- $pid; pid=$1
echo "PID $pid"
cat /proc/stat
if [ -n "$pid" ]; then
cat /proc/$pid/task/*/stat 2>/dev/null
for f in /proc/$pid/status /proc/$pid/io /proc/$pid/schedstat /proc/meminfo /proc/diskstats /proc/pressure/cpu /proc/pressure/io /proc/pressure/memory; do
  if [ -r "$f" ]; then
    printf 'RESOURCE %s\n' "$f"
    cat "$f"
  fi
done
fi'''


def parse(raw):
    total = idle = cores = 0
    threads = {}
    pid = ''
    for line in raw.splitlines():
        if line.startswith('PID '):
            pid = line[4:].strip()
        elif line.startswith('cpu '):
            values = [int(x) for x in line.split()[1:9]]
            total, idle = sum(values), values[3] + values[4]
        elif re.match(r'^cpu\d+ ', line):
            cores += 1
        elif line.startswith('T ') or re.match(r'^\d+ \(', line):
            # comm can contain spaces and parentheses; last ')' terminates it.
            end = line.rfind(')')
            begin = line.index('(')
            tid = line[(2 if line.startswith('T ') else 0):begin].strip()
            name = line[begin + 1:end]
            fields = line[end + 2:].split()  # state is field 3
            if len(fields) >= 20:
                threads[(tid, fields[19])] = (name, int(fields[11]) + int(fields[12]))
    return dict(total=total, idle=idle, cores=cores, pid=pid, threads=threads)


def summarize(before, after):
    delta = after['total'] - before['total']
    cores = after['cores']
    if delta <= 0 or not cores or cores != before['cores']:
        return 'ANDROID CPU: warming up', 'THREADS: warming up'
    busy = max(0.0, min(1.0, 1 - (after['idle'] - before['idle']) / delta))
    cpu = f'ANDROID CPU {busy * 100:.0f}% of {cores} cores ({busy * cores:.1f} cores busy) / 2s'
    if not after['pid'] or after['pid'] != before['pid']:
        return cpu, 'THREADS: waiting for game / first sample'
    ranked = []
    for identity, (name, ticks) in after['threads'].items():
        if identity in before['threads']:
            share = max(0, ticks - before['threads'][identity][1]) * cores * 100 / delta
            ranked.append((share, name))
    ranked.sort(reverse=True)
    labels = [f'{name[:15]} {value:.0f}%' for value, name in ranked[:3]]
    return cpu, 'THREADS (1 core=100%): ' + '  '.join(labels)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    parser.add_argument('--parent', type=int, required=True)
    parser.add_argument('--samples', type=int, default=0)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--package', required=True)
    parser.add_argument('--adb-port', type=int, default=5037)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--history', help='New JSONL path: 128 MiB cap; never overwritten')
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_.]+", args.package):
        parser.error("Invalid Android package name")
    command = COMMAND.replace("PACKAGE_NAME", args.package)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    parent = kernel.OpenProcess(0x00100000, False, args.parent)
    if not parent:
        raise RuntimeError('Cannot bind collector lifetime to launcher')
    before = None
    history = None
    resources = None
    if args.history:
        from resource_history import Resources
        history = open(args.history, 'x', encoding='utf-8', buffering=1)
        resources = Resources()
        history.write(json.dumps(dict(schema=1, kind='metadata', utc_ns=time.time_ns(),
            monotonic_ns=time.perf_counter_ns(), serial=args.serial, interval_s=2,
            cpu_denominator='Android ticks, thread=one guest core; host process=one host logical CPU',
            gpu_interval_s=10, max_bytes=128*1024*1024)) + '\n')
    try:
        index = 0
        while kernel.WaitForSingleObject(parent, 0) == 258:
            start = time.monotonic()
            record = dict(kind='sample', utc_ns=time.time_ns(), monotonic_ns=time.perf_counter_ns(), index=index)
            try:
                result = subprocess.run([args.adb, '-P', str(args.adb_port), '-s', args.serial, 'shell', command],
                    capture_output=True, text=True, timeout=4, creationflags=subprocess.CREATE_NO_WINDOW, check=True)
                after = parse(result.stdout)
                cpu, threads = summarize(before, after) if before else ('ANDROID CPU: warming up', 'THREADS: warming up')
                before = after
                record['android_proc_raw'] = result.stdout
            except (OSError, ValueError, subprocess.SubprocessError) as error:
                cpu, threads, before = 'ANDROID CPU: unavailable', 'THREADS: unavailable', None
                record['android_error'] = type(error).__name__
            if history:
                try:
                    record.update(cpu_display=cpu, threads_display=threads, windows=resources.sample(output.parent))
                    if index % 5 == 0:
                        from resource_history import gpu_sample
                        record['gpu'] = gpu_sample()
                    record['collection_ms'] = (time.monotonic() - start) * 1000
                    if history.tell() >= 128*1024*1024 or shutil.disk_usage(output.parent).free < 60*1024**3:
                        history.write(json.dumps(dict(kind='history_stopped', reason='size limit or 60 GiB free-space reserve')) + '\n')
                        history.close(); history = None
                    else:
                        history.write(json.dumps(record) + '\n')
                except (OSError, ValueError) as error:
                    print('Resource history stopped:', repr(error), flush=True)
                    history.close(); history = None
            temporary = output.with_suffix('.new')
            temporary.write_text(f'{int(time.time() * 1000)}\n{cpu}\n{threads}\n', encoding='ascii', errors='replace')
            # Windows readers may briefly deny rename/delete sharing. Retain the
            # old snapshot and retry rather than silently losing the collector.
            for attempt in range(10):
                try:
                    os.replace(temporary, output)
                    break
                except PermissionError:
                    time.sleep(.05)
            index += 1
            if args.samples and index >= args.samples:
                return
            time.sleep(max(.1, 2 - (time.monotonic() - start)))
    finally:
        if history: history.close()
        kernel.CloseHandle(parent)


if __name__ == '__main__':
    main()
