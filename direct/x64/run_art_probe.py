"""Build/run the managed x64 ART milestone. Failure is reported, never a pass.

Uses a disposable sysroot prepared by prepare_sysroot.py, not installed AXRB.
The JDK/build tools are developer dependencies only.
"""
import argparse
import os
from pathlib import Path
import subprocess
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runner', type=Path, required=True)
parser.add_argument('--sysroot', type=Path, required=True)
parser.add_argument('--work', type=Path, required=True)
parser.add_argument('--build-tools', type=Path, required=True)
parser.add_argument('--arm', action='store_true', help='Run the ARM JNI bridge integration instead')
parser.add_argument('--native-bridge', action='store_true', help='Use ART NativeBridge and ordinary System.load')
parser.add_argument('--linker', action='store_true', help='Test ARM dependency linking and constructors')
args = parser.parse_args()
args.native_bridge = args.native_bridge or args.linker
args.arm = args.arm or args.native_bridge
root, work = args.sysroot.resolve(), args.work.resolve()
inspection = subprocess.run([str(args.runner.resolve()), '--inspect', str(root / 'apex/com.android.art/bin/dalvikvm64')], capture_output=True, text=True, timeout=15)
if inspection.returncode:
    raise SystemExit(inspection.stderr)
if 'FSGSBASE=no' in inspection.stdout:
    raise SystemExit(77)
name = 'NativeArm' if args.arm else 'NativeArt'
log = 'arm-jni-probe' if args.arm else 'art-probe'
if args.native_bridge:
    log = 'nativebridge-probe'
marker = 'NATIVE_X64_ARM_JNI_PASS' if args.arm else 'NATIVE_X64_ART_PASS'
if args.linker:
    name, log, marker = 'NativeLink', 'arm-link-probe', 'NATIVE_X64_ARM_LINK_PASS'
classes = work / ('classes-arm' if args.arm else 'classes')
dex = root / 'data/local/tmp/x64-probe'
for directory in (classes, dex):
    directory.mkdir(parents=True, exist_ok=True)
shutil.copyfile(work / 'libnative-art-probe.so', dex / 'libnative-art-probe.so')
if args.arm:
    for library in ('libarm-jni-bridge.so','libarm-jni-fixture.so'):
        shutil.copyfile(work / library, dex / library)
if args.native_bridge:
    (dex / 'arm64').mkdir(exist_ok=True)
    shutil.copyfile(work / 'libarm-jni-fixture.so', dex / 'arm64/libarm-jni-fixture.so')
    shutil.copyfile(work / 'libaxrb_nativebridge.so', root / 'system/lib64/libaxrb_nativebridge.so')
if args.linker:
    for library in ('libarm-link-parent.so', 'libarm-link-dep.so', 'libarm-link-bad-init.so'):
        shutil.copyfile(work / library, dex / 'arm64' / library)
subprocess.run(['javac', '--release', '8', '-Xlint:-options', '-d', str(classes),
                str(Path(__file__).parent / 'tests' / (name+'.java'))], check=True)
subprocess.run(['java', '-cp', str(args.build_tools / 'lib/d8.jar'),
                'com.android.tools.r8.D8', '--min-api', '29', '--output', str(dex),
                *map(str, sorted(classes.glob(name+'*.class')))], check=True)
boot = [
    '/apex/com.android.art/javalib/core-oj.jar',
    '/apex/com.android.art/javalib/core-libart.jar',
    '/apex/com.android.art/javalib/okhttp.jar',
    '/apex/com.android.art/javalib/bouncycastle.jar',
    '/apex/com.android.art/javalib/apache-xml.jar',
    '/apex/com.android.i18n/javalib/core-icu4j.jar',
    '/apex/com.android.conscrypt/javalib/conscrypt.jar',
]
command = [str(args.runner.resolve()), '--run-linker', str(root),
           str(root / 'apex/com.android.art/bin/dalvikvm64'),
           '-Xuse-stderr-logger', '-Xint', '-Ximage:/nonexistent',
           '-Xbootclasspath:' + ':'.join(boot),
           '-cp', '/data/local/tmp/x64-probe/classes.dex', name]
if args.native_bridge:
    command[4:4] = ['-XX:NativeBridge=libaxrb_nativebridge.so', '-Xforce-nb-testing',
                    '-Djava.library.path=/data/local/tmp/x64-probe/arm64']
    command.append('standard')
with (work / (log+'.out')).open('wb') as stdout, (work / (log+'.err')).open('wb') as stderr:
    result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=20)
text = (work / (log+'.out')).read_text(errors='replace')
diagnostic = (work / (log+'.err')).read_text(errors='replace')
bridge_evidence = not args.native_bridge or all(value in diagnostic for value in (
    'AXRB_NATIVE_BRIDGE_INITIALIZED', 'AXRB_NATIVE_BRIDGE_LOAD',
    'AXRB_NATIVE_BRIDGE_TRAMPOLINE JNI_OnLoad', 'AXRB_NATIVE_BRIDGE_TRAMPOLINE Java_NativeArm_roundTrip'))
if args.linker:
    bridge_evidence = all(value in diagnostic for value in ('AXRB_NATIVE_BRIDGE_INITIALIZED', 'AXRB_NATIVE_BRIDGE_LOAD', 'Java_NativeLink_result'))
if result.returncode or marker not in text or not bridge_evidence:
    print((work / (log+'.err')).read_text(errors='replace')[-4000:])
    raise SystemExit(f'ART managed probe has NOT passed (exit {result.returncode}); logs: {work}')
print(text)
