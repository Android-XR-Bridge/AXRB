"""Launch one game configuration on the isolated test AVD and measure CPU per frame.

The measurement window reports, over the same wall-clock interval:
  * host CPU of the emulator process (all threads, in logical cores),
  * guest CPU from /proc/stat and per-thread ticks of the game process,
  * fresh (unique) game frames delivered to the host, from host.err TruePerf.
CPU cost per fresh frame is the figure that stays comparable when a
configuration changes the frame rate as well as the load.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

import psutil

ROOT = Path(__file__).resolve().parents[2]
MANAGED = Path.home() / 'AppData/Local/AXRB Runtime'
ADB = str(MANAGED / 'sdk/platform-tools/adb.exe')
SERIAL = 'emulator-5586'
AVD = 'axrb-digitalis-test'
GAMES = {
    'nexus': ('com.Ubisoft.ACNexusVR', 'com.Ubisoft.ACNexusVR/com.unity3d.player.UnityPlayerActivity'),
    'northstar': ('com.meta.samples.NorthStar', 'com.meta.samples.NorthStar/com.meta.northstar.NorthStarActivity'),
}


def adb(*args, timeout=30, check=True):
    result = subprocess.run([ADB, '-P', '5038', '-s', SERIAL, *args], capture_output=True, text=True,
                            encoding='utf-8', errors='replace', timeout=timeout)
    if check and result.returncode:
        raise RuntimeError(f'adb {args}: {result.stderr or result.stdout}')
    return result.stdout.strip()


def read(path):
    try:
        return Path(path).read_text(encoding='utf-8', errors='replace')
    except (FileNotFoundError, PermissionError):
        return ''


def qemu_process():
    for process in psutil.process_iter(['name', 'cmdline']):
        if (process.info['name'] or '').startswith('qemu-system') and AVD in ' '.join(process.info['cmdline'] or []):
            return process
    raise RuntimeError('Test emulator process not found')


GUEST_SNAPSHOT = r'''pid=$(pidof PKG); set -- $pid; pid=$1; echo "PID $pid"; head -1 /proc/stat; grep -c ^cpu /proc/stat
[ -n "$pid" ] && cat /proc/$pid/task/*/stat 2>/dev/null'''


def guest_snapshot(package):
    raw = adb('shell', GUEST_SNAPSHOT.replace('PKG', package))
    lines = raw.splitlines()
    pid = lines[0][4:].strip()
    values = [int(x) for x in lines[1].split()[1:9]]
    threads = {}
    for line in lines[3:]:
        end, begin = line.rfind(')'), line.find('(')
        if begin < 0:
            continue
        fields = line[end + 2:].split()
        threads[line[:begin].strip()] = (line[begin + 1:end], int(fields[11]), int(fields[12]))
    return dict(pid=pid, total=sum(values), idle=values[3] + values[4], cores=int(lines[2]) - 1,
                threads=threads, time=time.monotonic())


def interrupt_snapshot():
    """Guest interrupt counters (summed over CPUs) plus context switches."""
    counts = {}
    for line in adb('shell', 'cat /proc/interrupts; grep ^ctxt /proc/stat').splitlines():
        parts = line.split()
        if not parts:
            continue
        if parts[0] == 'ctxt':
            counts['ctxt'] = int(parts[1]); continue
        if not parts[0].endswith(':'):
            continue
        numbers = []
        for item in parts[1:]:
            if item.isdigit(): numbers.append(int(item))
            else: break
        if numbers:
            label = parts[0][:-1]
            if not label.isalpha():  # device lines: name them by their driver
                label = label + ':' + (parts[-1] if len(parts) > len(numbers) + 1 else '')
            counts[label] = sum(numbers)
    return counts


def truperf(text):
    return [{k: float(v) for k, v in re.findall(r'(\w+)=(-?[\d.]+)', line)}
            for line in text.splitlines() if 'AXRB TruePerf:' in line]


def arrival_rates(text):
    return [float(x) for x in re.findall(r'host-image-arrival: rate=([\d.]+)/s', text)]


def measure(package, seconds, host_log, simpleperf=None):
    qemu = qemu_process()
    host_before = truperf(read(host_log))
    arrivals_before = len(arrival_rates(read(host_log)))
    guest_before = guest_snapshot(package)
    irq_before = interrupt_snapshot()
    cpu_before = qemu.cpu_times()
    thread_before = {t.id: t.user_time + t.system_time for t in qemu.threads()}
    wall_before = time.monotonic()
    profile = None
    if simpleperf:
        profile = subprocess.Popen([ADB, '-P', '5038', '-s', SERIAL, 'shell',
            f'simpleperf record -p {guest_before["pid"]} --duration {seconds} -f 4000 -o /data/local/tmp/perf.data'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(seconds)
    wall = time.monotonic() - wall_before
    cpu_after = qemu.cpu_times()
    thread_after = {t.id: t.user_time + t.system_time for t in qemu.threads()}
    guest_after = guest_snapshot(package)
    irq_after = interrupt_snapshot()
    host_after = truperf(read(host_log))
    if profile:
        profile.wait(timeout=seconds + 60)
    host_cores = (cpu_after.user + cpu_after.system - cpu_before.user - cpu_before.system) / wall
    host_threads = sorted(((thread_after[t] - thread_before.get(t, 0)) / wall for t in thread_after), reverse=True)
    ticks = guest_after['total'] - guest_before['total']
    guest_cores = guest_after['cores']
    guest_busy = (1 - (guest_after['idle'] - guest_before['idle']) / ticks) * guest_cores
    threads = []
    if guest_after['pid'] == guest_before['pid']:
        for tid, (name, user, system) in guest_after['threads'].items():
            if tid in guest_before['threads']:
                _, user0, system0 = guest_before['threads'][tid]
                scale = guest_cores * 100 / ticks
                threads.append(dict(tid=tid, name=name, user=round((user - user0) * scale, 1),
                                    sys=round((system - system0) * scale, 1)))
    threads.sort(key=lambda t: -(t['user'] + t['sys']))
    game_total = sum(t['user'] + t['sys'] for t in threads) / 100
    fresh = None
    arrivals = arrival_rates(read(host_log))[arrivals_before:]
    if arrivals:
        fresh = sum(arrivals) / len(arrivals)
    if len(host_after) > len(host_before) and host_before:
        fresh = (host_after[-1]['fresh_total'] - host_before[-1]['fresh_total']) / \
                max(1e-9, (host_after[-1]['steady_ns'] - host_before[-1]['steady_ns']) / 1e9)
    window = host_after[len(host_before):]
    return dict(
        wall_s=round(wall, 2),
        host_qemu_cores=round(host_cores, 3),
        host_qemu_top_threads=[round(x, 3) for x in host_threads[:12]],
        guest_busy_cores=round(guest_busy, 3),
        guest_game_cores=round(game_total, 3),
        fresh_fps=round(fresh, 2) if fresh is not None else None,
        host_ms_per_fresh_frame=round(host_cores * 1000 / fresh, 2) if fresh else None,
        game_ms_per_fresh_frame=round(game_total * 1000 / fresh, 2) if fresh else None,
        low1_fps=[s.get('low1_fps') for s in window][-1:] ,
        worst_ms=max([s.get('worst_ms', 0) for s in window] or [0]),
        session_width=window[-1].get('width') if window else None,
        host_rate=[s.get('host_fps') for s in window],
        threads=threads[:14],
        guest_irq_per_s={k: round((irq_after[k] - irq_before.get(k, 0)) / wall)
                         for k in sorted(irq_after, key=lambda k: irq_before.get(k, 0) - irq_after[k])
                         if irq_after[k] - irq_before.get(k, 0) > wall * 20},
    )


def collect_profile(out, package):
    """Summaries computed on the guest, plus the translator's region map."""
    pid = adb('shell', f'pidof {package}').split()[0]
    base = 'simpleperf report -i /data/local/tmp/perf.data -n --percent-limit 0.05'
    reports = {
        'by_thread_dso.txt': f'{base} --sort comm,dso',
        'by_dso_sym.txt': f'{base} --sort dso,symbol',
        'by_thread_dso_sym.txt': f'{base} --sort comm,dso,symbol',
        'um_ips.txt': f'{base} --dsos unknown --sort vaddr_in_file',
    }
    for name, command in reports.items():
        try:
            (out / name).write_text(adb('shell', command, timeout=300), encoding='utf-8')
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            (out / name).write_text(f'FAILED: {error}', encoding='utf-8')
    (out / 'um_maps.txt').write_text(adb('shell', f'cat /proc/{pid}/maps'), encoding='utf-8')
    for candidate in [f'/data/data/{package}/perf-{pid}.map', f'/data/local/tmp/perf-{pid}.map']:
        text = adb('shell', f'cat {candidate} 2>/dev/null || true', timeout=120)
        if text:
            (out / 'um_perf.map').write_text(text, encoding='utf-8')
            break
    adb('pull', '/data/local/tmp/perf.data', str(out / 'perf.data'), timeout=300)


AVD_CONFIG = MANAGED / 'avd' / f'{AVD}.avd' / 'config.ini'


def set_avd_config(overrides):
    """Apply key=value lines to the test AVD config; returns the previous text."""
    text = AVD_CONFIG.read_text(encoding='utf-8')
    if not re.search(r'(?m)^AvdId=' + re.escape(AVD) + r'\r?$', text):
        raise SystemExit('Unexpected test AVD config')
    updated = text
    for item in overrides:
        key, value = item.split('=', 1)
        updated = re.sub(r'(?m)^' + re.escape(key) + r'=.*\r?\n?', '', updated)
        updated = updated.rstrip('\r\n') + f'\n{key}={value}\n'
    AVD_CONFIG.write_text(updated, encoding='utf-8')
    return text


def boot_emulator(log, cores=4, cold=False, exit_stats=None, hv_apic=False, transport='Asg', no_wake=False):
    boot = subprocess.run(['powershell', '-ExecutionPolicy', 'Bypass', '-File',
        str(ROOT / 'scripts/emulator/windows_android_emulator.ps1'), '-Action', 'Start', '-Sdk', str(MANAGED / 'sdk'),
        '-Avd', AVD, '-Port', '5586', '-ApiLevel', '36', '-Abi', 'arm64-v8a', '-MemoryMB', '8192',
        '-CpuCores', str(cores), '-GuestClock', 'TscCorrected', '-GpuSharing', '-LocalApic', 'Qemu',
        '-GraphicsTransport', transport] + (['-ColdBoot'] if cold else []),
        env=dict(env_for(ROOT / 'out/cpu-work/data'), **({'AXRB_WHPX_EXIT_STATS': str(exit_stats)} if exit_stats else {}),
                 **({'AXRB_WHPX_HV_APIC': '1'} if hv_apic else {}),
                 **({'AXRB_WHPX_HV_X2APIC': '1'} if hv_apic == 'x2apic' else {}),
                 **({'AXRB_WHPX_HV_APIC_WAKE': '0'} if no_wake else {})),
        stdout=Path(log).open('w'), stderr=subprocess.STDOUT,
        stdin=subprocess.DEVNULL, timeout=600)
    if boot.returncode:
        raise SystemExit('Emulator start failed: ' + read(log)[-1500:])


def env_for(data_home):
    return dict(**__import__('os').environ, AXRB_DATA_HOME=str(data_home),
                ANDROID_AVD_HOME=str(MANAGED / 'avd'), ANDROID_USER_HOME=str(MANAGED / 'android'),
                ANDROID_EMULATOR_HOME=str(MANAGED / 'android'))


# Read-only properties this harness may change. Every run writes the wanted
# value (default unless overridden), so an interrupted run cannot leak its
# setting into the next one.
DEFAULT_BUILD_PROPS = {'ro.berberis.flags': 'accurate-sigsegv'}


def apply_build_props(overrides):
    """Make /system/build.prop hold the wanted values. Returns True when it
    changed; the caller must then restart the emulator (a guest reboot inside
    a running emulator leaves graphics at a few frames per second)."""
    wanted = dict(DEFAULT_BUILD_PROPS, **overrides)
    lines = adb('shell', 'cat /system/build.prop').splitlines()
    missing = {k: v for k, v in wanted.items() if f'{k}={v}' not in lines}
    if not missing:
        return False
    adb('remount', timeout=60)
    for key, value in missing.items():
        if not any(line.startswith(key + '=') for line in lines):
            raise SystemExit(f'{key} is not defined in /system/build.prop')
        adb('shell', f"sed -i 's/^{key}=.*/{key}={value}/' /system/build.prop && sync")
    return True


def check_build_props(overrides):
    for key, value in dict(DEFAULT_BUILD_PROPS, **overrides).items():
        actual = adb('shell', f'getprop {key}')
        if actual != value:
            raise SystemExit(f'{key} is {actual!r}, wanted {value!r}')


def stop_emulator():
    adb('emu', 'kill', check=False)
    for _ in range(60):
        try:
            qemu_process()
        except RuntimeError:
            return
        time.sleep(1)
    qemu_process().kill()
    time.sleep(3)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game', choices=GAMES, default='nexus')
    p.add_argument('--label', required=True)
    p.add_argument('--seconds', type=int, default=30)
    p.add_argument('--warmup', type=int, default=45)
    p.add_argument('--cores', type=int, default=4)
    p.add_argument('--unity-args', default='')
    p.add_argument('--setprop', action='append', default=[], help='key=value, restored afterwards')
    p.add_argument('--simpleperf', action='store_true')
    p.add_argument('--keep', action='store_true', help='leave the game running after measuring')
    p.add_argument('--cold', action='store_true', help='cold boot the emulator')
    p.add_argument('--synthetic-hmd', action='store_true', help='host reports a fixed valid head pose')
    p.add_argument('--exit-stats', action='store_true', help='WHPX exit profile in exits.txt')
    p.add_argument('--hv-apic', action='store_true', help='hypervisor-emulated local APIC (clock helper)')
    p.add_argument('--hv-x2apic', action='store_true', help='--hv-apic with the guest in x2APIC mode')
    p.add_argument('--transport', choices=['Asg', 'Pipe'], default='Asg', help='graphics command transport')
    p.add_argument('--hv-no-wake', action='store_true', help='hypervisor APIC without the wake interrupt on kicks')
    p.add_argument('--capture', action='store_true', help='host saves four projection frames (PPM)')
    p.add_argument('--avd-config', action='append', default=[], help='key=value for this boot only')
    p.add_argument('--guest-shell', action='append', default=[], help='root shell command run before launch')
    p.add_argument('--build-prop', action='append', default=[], help='ro.key=value in /system/build.prop (reboots)')
    p.add_argument('--repeat', type=int, default=1, help='measurement windows in one session')
    a = p.parse_args()
    package, activity = GAMES[a.game]
    out = ROOT / 'out/cpu-work/runs' / f'{time.strftime("%Y%m%d-%H%M%S")}-{a.label}'
    out.mkdir(parents=True)
    data_home = ROOT / 'out/cpu-work/data'
    logs = data_home / 'logs/game'
    for name in ['host.err', 'guest.log']:
        (logs / name).unlink(missing_ok=True)
    try:
        qemu_process()
        raise SystemExit('Stop the test emulator first; each run boots its own for comparable state')
    except RuntimeError:
        pass
    build_props = dict(item.split('=', 1) for item in a.build_prop)
    for attempt in range(2):
        original_config = set_avd_config(a.avd_config) if a.avd_config else None
        try:
            boot_emulator(out / 'boot.log', a.cores, a.cold or bool(a.avd_config),
                          exit_stats=(out / 'exits.txt') if a.exit_stats else None,
                          hv_apic='x2apic' if a.hv_x2apic else a.hv_apic, transport=a.transport, no_wake=a.hv_no_wake)
        finally:
            if original_config is not None:
                AVD_CONFIG.write_text(original_config, encoding='utf-8')
        if AVD not in adb('emu', 'avd', 'name').splitlines():
            raise SystemExit('Isolated test AVD required')
        if a.hv_apic or a.hv_x2apic:
            emulator_err = (data_home / 'logs/emulator/emulator.stderr.log').read_text(errors='replace')
            if 'local APIC emulated by the hypervisor' not in emulator_err:
                raise SystemExit('The hypervisor APIC was requested but is not active')
            if a.hv_x2apic and 'x2apic enabled' not in (data_home / 'logs/emulator/emulator.stdout.log').read_text(errors='replace'):
                raise SystemExit('x2APIC was requested but the guest did not enable it')
        adb('root', check=False)
        adb('wait-for-device')
        time.sleep(3)
        if attempt or not apply_build_props(build_props):
            break
        stop_emulator()
        (out / 'exits.txt').unlink(missing_ok=True)
    check_build_props(build_props)
    for item in a.setprop:
        key, value = item.split('=', 1)
        adb('shell', f"setprop {key} '{value}'")
    for command in a.guest_shell:
        adb('shell', command)
    results_props = adb('shell', "getprop | grep -iE 'berberis|ndk_translation|native.bridge'")
    env = env_for(data_home)
    if a.capture:
        env['AXRB_CAPTURE_PREFIX'] = str(out / 'frame')
    if a.synthetic_hmd:
        env['AXRB_TEST_SYNTHETIC_HMD'] = '1'
    command = ['powershell', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT / 'scripts/run/run_windows_game.ps1'),
               '-Package', package, '-Activity', activity, '-Sdk', str(MANAGED / 'sdk'), '-Avd', AVD,
               '-Port', '5586', '-MemoryMB', '8192', '-CpuCores', str(a.cores), '-GuestClock', 'TscCorrected',
               '-GpuSharing', '-FpsHud', '-CaptureGuestLog', '-OwnsEmulator',
               '-HostExe', str(ROOT / 'out/host/bin/Release/axrb-host-bridge.exe')]
    if a.unity_args:
        command += ['-UnityArguments', a.unity_args]
    host_pid = None
    results = dict(label=a.label, game=a.game, unity_args=a.unity_args, setprop=a.setprop, build_prop=a.build_prop,
                   translator_props=results_props, guest_shell=a.guest_shell, avd_config=a.avd_config, synthetic_hmd=a.synthetic_hmd, hv_apic='x2apic' if a.hv_x2apic else a.hv_apic, windows=[])
    start = time.monotonic()
    try:
        with (out / 'launch.log').open('w') as launch_log:
            process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=launch_log, stderr=subprocess.STDOUT)
            try:
                while True:
                    if time.monotonic() - start > 240:
                        raise RuntimeError('No fresh frames within 240 seconds')
                    if process.poll() is not None:
                        raise RuntimeError('Launcher exited: ' + read(out / 'launch.log')[-2000:])
                    launch = read(out / 'launch.log')
                    match = re.search(r'SteamVR identity: .*?"pid":\s*(\d+)', launch)
                    if match:
                        host_pid = int(match[1])
                    if arrival_rates(read(logs / 'host.err')):
                        break
                    time.sleep(1)
                results['first_frame_s'] = round(time.monotonic() - start, 1)
                print(f'first fresh frame after {results["first_frame_s"]}s; warming up {a.warmup}s', flush=True)
                time.sleep(a.warmup)
                for index in range(a.repeat):
                    window = measure(package, a.seconds, logs / 'host.err',
                                     simpleperf=a.simpleperf and index == 0)
                    results['windows'].append(window)
                    print(json.dumps({k: v for k, v in window.items() if k != 'threads'}), flush=True)
                    for t in window['threads'][:6]:
                        print('   ', t, flush=True)
                if a.simpleperf:
                    collect_profile(out, package)
                if a.keep:
                    print('Leaving the session running.', flush=True)
                    return
            finally:
                if not a.keep:
                    if not host_pid:
                        for proc in psutil.process_iter(['name']):
                            if proc.info['name'] == 'axrb-host-bridge.exe':
                                host_pid = proc.pid
                    if host_pid:
                        subprocess.run(['powershell', '-NoProfile', '-Command',
                            f'$p=Get-Process -Id {host_pid} -ErrorAction SilentlyContinue; if($p){{[void]$p.CloseMainWindow()}}'],
                            timeout=15)
                    try:
                        process.wait(timeout=40)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        adb('shell', f'am force-stop {package}', check=False)
                    for proc in psutil.process_iter(['name']):
                        if proc.info['name'] == 'axrb-host-bridge.exe':
                            proc.kill()
    finally:
        if not a.keep:
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                try:
                    qemu_process()
                except RuntimeError:
                    break
                time.sleep(1)
            else:
                adb('emu', 'kill', check=False)
                time.sleep(5)
                for proc in psutil.process_iter(['name', 'cmdline']):
                    if (proc.info['name'] or '').startswith('qemu-system') and AVD in ' '.join(proc.info['cmdline'] or []):
                        proc.kill()
        for name in ['host.err', 'guest.log']:
            (out / name).write_text(read(logs / name), encoding='utf-8')
        (out / 'result.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
        print('saved', out, flush=True)


if __name__ == '__main__':
    main()
