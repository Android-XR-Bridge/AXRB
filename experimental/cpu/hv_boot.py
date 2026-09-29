"""Boot the test AVD with the hypervisor-emulated APIC and report guest SMP/timer health.

  python experimental/cpu/hv_boot.py [--kernel 'apic=debug'] [--off]
"""
import argparse
import re
import subprocess
import time

import session
from session import ADB, AVD_CONFIG, ROOT, SERIAL, adb, boot_emulator, set_avd_config

EMULATOR_LOG = ROOT / 'out/cpu-work/data/logs/emulator'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--kernel', default='', help='extra kernel parameters for this boot')
    p.add_argument('--off', action='store_true', help='boot without the hypervisor APIC (reference)')
    p.add_argument('--x2apic', action='store_true', help='guest in x2APIC mode')
    p.add_argument('--cores', type=int, default=4)
    a = p.parse_args()
    subprocess.run(['python', str(ROOT / 'experimental/cpu/kill_run.py')])
    original = set_avd_config([f'kernel.parameters={a.kernel}']) if a.kernel else None
    try:
        boot_emulator(ROOT / 'out/cpu-work/boot-hvapic.log', a.cores, cold=bool(a.kernel), hv_apic=False if a.off else ('x2apic' if a.x2apic else True))
    finally:
        if original is not None:
            AVD_CONFIG.write_text(original, encoding='utf-8')
    adb('root', check=False)
    time.sleep(3)
    adb('wait-for-device')
    print(adb('shell', 'cat /sys/devices/system/cpu/online; grep -E "LOC|CAL|RES|timer" /proc/interrupts; '
                       'cat /sys/devices/system/clockevents/clockevent0/current_device'))
    kernel = (EMULATOR_LOG / 'emulator.stdout.log').read_text(encoding='utf-8', errors='replace')
    for line in kernel.splitlines():
        if re.search(r'^\[ *\d+\.\d+\] .*(APIC|apic|smp|CPU\d|TIMER|calibrat|lapic|jiffies|Clockevents)', line):
            print(line)
    print('\n'.join(l for l in (EMULATOR_LOG / 'emulator.stderr.log').read_text(errors='replace').splitlines()
                    if 'hv-apic' in l)[-1500:])


if __name__ == '__main__':
    main()
