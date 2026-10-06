"""Refract-derived guest performance settings, scoped to the Android emulator."""
import subprocess


def apply_performance_policy(run):
    if run('shell', 'getprop ro.hardware') not in ('ranchu', 'goldfish'):
        return {'status': 'skipped'}
    report = {}
    commands = {
        # WHPX makes the cross-vCPU IPI used by TTWU_QUEUE expensive.
        'scheduler': 'mountpoint -q /sys/kernel/debug || mount -t debugfs debugfs /sys/kernel/debug; '
                     'echo NO_TTWU_QUEUE > /sys/kernel/debug/sched/features && cat /sys/kernel/debug/sched/features',
        # A remount can prevent Android loading zram during boot. Never reset
        # an active swap device. Cap the replacement at 75% of guest RAM.
        'swap': "grep -q '^/dev/.*zram0 ' /proc/swaps && { echo active; exit 0; }; "
                'd=/system_dlkm/lib/modules; '
                '[ -e /sys/block/zram0 ] || { insmod $d/zsmalloc.ko; insmod $d/zram.ko; }; '
                '[ -e /sys/block/zram0/disksize ] || exit 1; '
                "size=$(awk '/^MemTotal:/ {printf \"%.0f\", $2 * 768}' /proc/meminfo); "
                '[ "$size" -gt 0 ] || exit 1; '
                '[ "$(cat /sys/block/zram0/disksize)" -gt 0 ] || echo "$size" > /sys/block/zram0/disksize; '
                'mkswap /dev/block/zram0 && swapon /dev/block/zram0 && cat /proc/swaps',
    }
    for name, command in commands.items():
        try:
            output = run('shell', command)
            report[name] = ('NO_TTWU_QUEUE' if 'NO_TTWU_QUEUE' in output.split() else 'unchanged') if name == 'scheduler' else 'active'
        except (subprocess.SubprocessError, OSError) as error:
            # Optional kernel features must not prevent launching a game.
            report[name] = {'status': 'unavailable', 'reason': str(error)[:300]}
    bridge = run('shell', 'getprop ro.dalvik.vm.native.bridge')
    report['translator'] = bridge
    if bridge == 'libberberis_arm64.so':
        run('shell', 'setprop berberis.gearup_min_insns 0')
        report['gearup_min_insns'] = run('shell', 'getprop berberis.gearup_min_insns')
        if report['gearup_min_insns'] != '0':
            raise RuntimeError('Digitalis gear-up policy did not apply')
        run('shell', 'cat /system/bin/arm64/app_process64 /system/bin/arm64/linker64 > /dev/null && ls /system/lib64/arm64 > /dev/null')
    return report
