"""Stage and run one trusted ARM APK using native x86 ART, without a hypervisor.

Developer experiment only. Requires prepared x64 and ARM AOSP sysroots and a
running AXRB host bridge. Keeps app data in the experimental x64 sysroot.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_art_session import boot_classpath, required, stage_libraries
from prepare_properties import generate

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sysroot', 'arm-sysroot', 'build', 'arm-build', 'apk', 'bootstrap', 'proxies', 'gpu-dir', 'logs'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--activity', required=True)
    p.add_argument('--seconds', type=int, default=0, help='Diagnostic timeout; 0 keeps interactive play running')
    a = p.parse_args()
    if not 0 <= a.seconds <= 600:
        p.error('--seconds must be 0 (interactive) or between 1 and 600')
    root, build, arm_build = a.sysroot.resolve(), a.build.resolve(), a.arm_build.resolve()
    runner = required(build / 'Release/axrb-native-x64.exe')
    service = required(arm_build / 'Release/axrb-arm-service.dll')
    helper = required(arm_build / 'native-arm-helper')
    platform = build / 'platform'
    # Never overwrite the currently running experiment's staged code.
    stage = root / 'data/local/tmp/native-game'
    libraries = stage / 'lib/arm64'
    libraries.mkdir(parents=True, exist_ok=True)
    source = stage_libraries(required(a.apk), root / 'data/local/tmp/native-game-cache')
    # This directory contains only harness-managed native libraries, no saves.
    for old in libraries.iterdir():
        if old.is_file(): old.unlink()
    for file in source.glob('*.so'):
        if file.name not in ('libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libc++.so'):
            shutil.copyfile(file, libraries / file.name)
    for name in ('libEGL.so', 'libGLESv1_CM.so', 'libGLESv2.so', 'libGLESv3.so',
                 'libvulkan.so', 'libaaudio.so', 'libopenxr_runtime.so'):
        shutil.copyfile(required(a.proxies / name), libraries / name)
    for name in ('libandroid.so', 'libOpenSLES.so'):
        shutil.copyfile(required(platform / 'arm64' / name), libraries / name)
    for name, source_file in (
        ('libaxrb_nativebridge.so', build / 'libaxrb_nativebridge.so'),
        ('libaxrb_art_bridge.so', platform / 'libaxrb_art_bridge.so'),
        ('libaxrb_art_bridge.so.axrb-code', platform / 'libaxrb_art_bridge.so.axrb-code')):
        shutil.copyfile(required(source_file), root / 'system/lib64' / name)
    shutil.copyfile(required(a.apk), stage / 'base.apk')
    shutil.copyfile(required(a.bootstrap), stage / 'bootstrap.dex')
    generate(root)
    guest = '/data/local/tmp/native-game'
    command = [str(runner), '--run-linker', str(root), str(required(root / 'system/bin/app_process64')),
               '-Xforce-nb-testing', '-XX:NativeBridge=libaxrb_nativebridge.so',
               '-Xint', '-Xuse-stderr-logger', '-Ximage:/nonexistent',
               '-Xbootclasspath:' + boot_classpath(root) + ':' + guest + '/bootstrap.dex',
               '-Djava.class.path=' + guest + '/bootstrap.dex', '/system/bin',
               'com.axrb.runtime.AppProcess', guest + '/base.apk', a.activity]
    env = dict(os.environ, AXRB_X64_ARM_SERVICE=str(service), AXRB_X64_ARM_SYSROOT=str(a.arm_sysroot.resolve()),
               AXRB_X64_ARM_HELPER=str(helper), AXRB_X64_ARM_LIBRARIES=str(libraries),
               AXRB_X64_DATA=str(root / 'data'), AXRB_X64_APK=str(stage / 'base.apk'),
               AXRB_X64_WRITABLE_DATA='1', AXRB_X64_TIMEOUT_MS=str(a.seconds * 1000),
               AXRB_DIRECT_TRANSLATOR='digitalis', AXRB_GPU_LAYER_DIR=str(a.gpu_dir.resolve()))
    a.logs.mkdir(parents=True, exist_ok=True)
    (a.logs / 'command.json').write_text(json.dumps(command, indent=2))
    with (a.logs / 'game.out').open('wb') as out, (a.logs / 'game.err').open('wb') as err:
        process = subprocess.Popen(command, env=env, stdout=out, stderr=err)
        (a.logs / 'game.pid').write_text(str(process.pid))
        try:
            return process.wait(timeout=a.seconds + 15 if a.seconds else None)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)

if __name__ == '__main__':
    raise SystemExit(main())
