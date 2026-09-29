"""Switch the isolated test AVD between the stock Google translator and Digitalis.

Both sets of guest libraries stay on the test AVD's writable overlay:
/system/lib64/arm64.axrb-stock is the image's own copy (made by the Digitalis
installer) and /system/lib64/arm64.digitalis is kept here on first switch.
The emulator must be running; it is rebooted to apply the change.
"""
import argparse
import subprocess
import time

from session import ADB, AVD, SERIAL

BRIDGE = {'stock': 'libndk_translation.so', 'digitalis': 'libberberis_arm64.so'}


def adb(*args, timeout=120):
    result = subprocess.run([ADB, '-P', '5038', '-s', SERIAL, *args], capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout.strip()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('target', choices=BRIDGE)
    a = p.parse_args()
    if AVD not in adb('emu', 'avd', 'name').splitlines():
        raise SystemExit('Refusing to change any AVD except ' + AVD)
    adb('root'); adb('wait-for-device'); adb('remount')
    suffix = 'axrb-stock' if a.target == 'stock' else 'digitalis'
    # Both guest trees are kept beside the active one; copy the wanted tree.
    for directory in ['lib64', 'bin']:
        base = f'/system/{directory}/arm64'
        adb('shell', f'test -d {base}.{suffix} && rm -rf {base} && cp -a {base}.{suffix} {base}')
    adb('shell', f'cp -p /system/etc/ld.config.arm64.txt.{suffix} /system/etc/ld.config.arm64.txt')
    adb('shell', f"sed -i 's/^ro.dalvik.vm.native.bridge=.*/ro.dalvik.vm.native.bridge={BRIDGE[a.target]}/' /system/build.prop")
    # Overlayfs userxattr lookup requires readable directories for app UIDs.
    adb('shell', 'chmod 755 /system/bin/arm64 /system/lib64/arm64; find /system/lib64/arm64 -type d -exec chmod 755 {} \;')
    adb('shell', 'restorecon -RF /system/lib64/arm64 /system/bin/arm64 /system/etc/ld.config.arm64.txt /system/build.prop; sync')
    adb('reboot')
    time.sleep(10)
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        try:
            if adb('shell', 'getprop sys.boot_completed', timeout=10) == '1':
                break
        except (RuntimeError, subprocess.TimeoutExpired):
            pass
        time.sleep(3)
    else:
        raise SystemExit('Test AVD did not boot')
    print('native bridge:', adb('shell', 'getprop ro.dalvik.vm.native.bridge'))


if __name__ == '__main__':
    main()
