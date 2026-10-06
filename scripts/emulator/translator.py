"""Install a verified Digitalis bundle on the managed Android 16 emulator."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shlex
import subprocess
import tempfile
import time

SUPPORTED_FINGERPRINTS = {
    'google/sdk_gphone64_x86_64/emu64xa:16/BE2A.250530.026.F3/13894323:userdebug/dev-keys',
}

def verify_bundle(bundle):
    expected = {}
    for line in (bundle / 'SHA256SUMS').read_text().splitlines():
        if not line.strip(): continue
        digest, name = line.split(maxsplit=1)
        name = name.lstrip('*')
        relative = PurePosixPath(name)
        if (not re.fullmatch('[a-fA-F0-9]{64}', digest) or relative.is_absolute()
                or '..' in relative.parts or '\\' in name or ':' in name):
            raise ValueError('Invalid Digitalis checksum entry')
        file = bundle.joinpath(*relative.parts)
        if not file.resolve().is_relative_to(bundle.resolve()):
            raise ValueError('Digitalis file leaves its bundle')
        if hashlib.sha256(file.read_bytes()).hexdigest() != digest.lower():
            raise ValueError(f'Digitalis checksum mismatch: {name}')
        expected[name] = digest.lower()
    files = {p.relative_to(bundle).as_posix() for p in (bundle / 'system').rglob('*') if p.is_file()}
    if not files or not files.issubset(expected):
        raise ValueError('Digitalis has unchecked system files')
    for name in ('system/lib64/libberberis_arm64.so', 'system/lib64/arm64/libc.so',
                 'system/bin/arm64/linker64', 'system/etc/ld.config.arm64.txt'):
        if name not in expected: raise ValueError(f'Missing Digitalis artifact: {name}')
    for name, machine in (('system/lib64/libberberis_arm64.so', 62), ('system/lib64/arm64/libc.so', 183)):
        data = (bundle / name).read_bytes()[:20]
        if data[:6] != b'\x7fELF\x02\x01' or len(data) != 20 or int.from_bytes(data[18:20], 'little') != machine:
            raise ValueError(f'Wrong Digitalis ELF architecture: {name}')
    return expected['system/lib64/libberberis_arm64.so']


def payload_install_command(bundle, staging):
    # adb push assigns staging ownership/modes. Copy only payload files so
    # these attributes never replace /system, /system/bin, etc. themselves.
    commands = []
    for file in sorted((bundle / 'system').rglob('*')):
        if not file.is_file(): continue
        relative = file.relative_to(bundle).as_posix()
        destination = '/' + relative
        parent = str(PurePosixPath(destination).parent)
        quoted_parent = shlex.quote(parent)
        commands.append(f'if [ ! -d {quoted_parent} ]; then mkdir -p {quoted_parent} && '
                        f'chown 0:0 {quoted_parent} && chmod 755 {quoted_parent}; fi')
        quoted = shlex.quote(destination)
        mode = '755' if relative.startswith('system/bin/') else '644'
        commands.append(f'cp {shlex.quote(staging + "/" + relative)} {quoted} && '
                        f'chown 0:0 {quoted} && chmod {mode} {quoted}')
    return ' && '.join(commands)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--bundle', type=Path)
    parser.add_argument('--use', choices=('auto', 'digitalis', 'google'), default='auto')
    parser.add_argument('--verify-only', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    bundle = args.bundle or Path(os.environ.get('AXRB_DIGITALIS_BUNDLE', root / 'out/digitalis/prebuilts'))
    if args.use == 'auto' and not bundle.is_dir():
        print(json.dumps({'status': 'unchanged', 'reason': 'No built Digitalis bundle'})); return
    digest = verify_bundle(bundle) if args.use != 'google' else ''
    install_id = ('2:' + hashlib.sha256((bundle / 'SHA256SUMS').read_bytes()).hexdigest()) if digest else ''
    marker = '/system/etc/axrb-digitalis-install'
    if args.verify_only:
        print(json.dumps({'status': 'verified', 'sha256': digest})); return
    adb = [str(args.sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    def run(*parts, timeout=120):
        return subprocess.run([*adb, *parts], check=True, capture_output=True, text=True, timeout=timeout).stdout.strip()
    def shell(command): return run('shell', command)
    def boot():
        run('reboot')
        deadline = time.monotonic() + 600
        while time.monotonic() < deadline:
            try:
                if shell('getprop sys.boot_completed') == '1': return
            except subprocess.SubprocessError: pass
            time.sleep(2)
        raise RuntimeError('Android did not finish rebooting after translator installation')
    if shell('getprop ro.hardware') not in ('ranchu', 'goldfish'):
        raise RuntimeError('Translator installation is restricted to Android emulators')
    if shell('getprop ro.build.version.sdk') != '36' or shell('getprop ro.product.cpu.abi') != 'x86_64':
        if args.use == 'auto':
            print(json.dumps({'status': 'unchanged', 'reason': 'Digitalis requires API 36 x86_64'})); return
        raise RuntimeError('Digitalis requires API 36 x86_64')
    target = 'libndk_translation.so' if args.use == 'google' else 'libberberis_arm64.so'
    if args.use != 'google' and shell('getprop ro.build.fingerprint') not in SUPPORTED_FINGERPRINTS:
        if args.use == 'auto':
            print(json.dumps({'status': 'unchanged', 'reason': 'Digitalis is not validated for this Android image'})); return
        raise RuntimeError('Digitalis private Bionic ABI does not support this Android image')
    current = shell('getprop ro.dalvik.vm.native.bridge')
    if current == target:
        remote = shell('sha256sum /system/lib64/libberberis_arm64.so 2>/dev/null || true').split() if digest else []
        installed = shell(f'cat {marker} 2>/dev/null || true') if digest else ''
        if not digest or (remote and remote[0] == digest and installed == install_id):
            print(json.dumps({'status': 'ready', 'translator': target, 'sha256': digest})); return
    from android_runtime_policy import ensure_adb_root
    ensure_adb_root(run)
    run('remount')
    try:
        shell('touch /system/.axrb-translator-write && rm /system/.axrb-translator-write')
    except subprocess.CalledProcessError:
        boot(); ensure_adb_root(run); run('remount')
        shell('touch /system/.axrb-translator-write && rm /system/.axrb-translator-write')
    backup = '/system/etc/axrb-google-translator'
    if shell(f'[ -f {backup}/complete ] && echo yes || echo no') != 'yes':
        if current != 'libndk_translation.so':
            raise RuntimeError('Original Google translator backup is missing; refusing to overwrite this installation')
        # A prior interrupted backup has no completion marker. Recreate it
        # from the still-active Google installation, rather than nesting cp.
        shell(f'rm -rf {backup} && mkdir -p {backup} && chmod 700 {backup} && cp -a /system/lib64/arm64 {backup}/lib64 && '
              f'cp -a /system/bin/arm64 {backup}/bin && cp /system/etc/ld.config.arm64.txt {backup}/ld.config.arm64.txt && '
              f'cp /system/build.prop {backup}/build.prop && touch {backup}/complete')
    if args.use != 'google':
        staging = '/data/local/tmp/axrb-digitalis-' + digest[:16]
        shell('mkdir -p ' + staging)
        run('push', str(bundle / 'system'), staging, timeout=300)
        # Check the transferred payload before stopping Android or changing /system.
        for file in (bundle / 'system').rglob('*'):
            if not file.is_file(): continue
            relative = file.relative_to(bundle).as_posix()
            checksum = shell('sha256sum ' + shlex.quote(staging + '/' + relative)).split()[0]
            if checksum != hashlib.sha256(file.read_bytes()).hexdigest():
                raise RuntimeError(f'Digitalis transfer checksum mismatch: {relative}')
        # Push a script instead of passing a potentially oversized Windows argv.
        with tempfile.TemporaryDirectory(prefix='axrb-translator-') as temporary:
            script = Path(temporary) / 'install.sh'
            script.write_text('#!/system/bin/sh\nset -e\n' + payload_install_command(bundle, staging) + '\n',
                              encoding='utf-8', newline='\n')
            run('push', str(script), staging + '/install.sh')
    shell('stop')
    try:
        # Keep the complete original set on-device for explicit rollback.
        if args.use == 'google':
            shell(f'rm -rf /system/lib64/arm64 /system/bin/arm64 && '
                  f'cp -a {backup}/lib64 /system/lib64/arm64 && cp -a {backup}/bin /system/bin/arm64 && '
                  f'cp {backup}/ld.config.arm64.txt /system/etc/ld.config.arm64.txt')
        else:
            shell('rm -rf /system/lib64/arm64 /system/bin/arm64 && rm -f /system/lib64/libberberis_*.so && ' +
                  'sh ' + staging + '/install.sh')
        if args.use == 'google':
            shell(f'rm -f {marker}')
        else:
            shell(f'echo {shlex.quote(install_id)} > {marker} && chown 0:0 {marker} && chmod 644 {marker}')
        shell("sed -i 's/^ro.dalvik.vm.native.bridge=.*/ro.dalvik.vm.native.bridge=" + target + "/' /system/build.prop")
        if shell('grep ^ro.dalvik.vm.native.bridge= /system/build.prop') != 'ro.dalvik.vm.native.bridge=' + target:
            raise RuntimeError('Native bridge property was not updated')
        shell('restorecon -R /system/lib64 /system/bin /system/etc; sync')
        boot()
        if shell('getprop ro.dalvik.vm.native.bridge') != target:
            raise RuntimeError('Android did not select the requested native bridge')
    except Exception as install_error:
        try:
            ensure_adb_root(run)
            run('remount')
            shell('stop')
            shell(f'rm -f {marker}')
            shell(f'rm -rf /system/lib64/arm64 /system/bin/arm64 && '
                  f'cp -a {backup}/lib64 /system/lib64/arm64 && cp -a {backup}/bin /system/bin/arm64 && '
                  f'cp {backup}/ld.config.arm64.txt /system/etc/ld.config.arm64.txt && cp {backup}/build.prop /system/build.prop && '
                  'restorecon -R /system/lib64/arm64 /system/bin/arm64 /system/etc/ld.config.arm64.txt; sync')
            boot()
        except Exception as rollback_error:
            raise RuntimeError(f'Translator installation failed ({install_error}); rollback failed ({rollback_error}). '
                               f'The original files remain in {backup}.') from install_error
        raise
    print(json.dumps({'status': 'installed', 'translator': target, 'sha256': digest}))


if __name__ == '__main__': main()
