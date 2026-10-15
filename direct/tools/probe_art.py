"""Exercise a real ART DEX program in an isolated Direct runtime directory."""
import argparse
from pathlib import Path
import subprocess
import shutil
import re


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', type=Path, required=True)
    p.add_argument('--sysroot', type=Path, required=True)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--log', type=Path, required=True)
    p.add_argument('--timeout', type=int, default=120)
    p.add_argument('--main', default='ArtSmoke')
    p.add_argument('--dex', type=Path, help='DEX containing the selected main class')
    p.add_argument('--arg', action='append', default=[], help='Argument for the Java entry point')
    p.add_argument('--expect', default='ART_SMOKE_PASS')
    p.add_argument('--mount-ro', action='append', default=[])
    p.add_argument('--prop', action='append', default=[])
    p.add_argument('--host-graphics', action='store_true')
    p.add_argument('--framework', action='store_true')
    p.add_argument('--check-jni', action='store_true')
    a = p.parse_args()
    # Android rejects writable dynamically loaded DEX files. Put the fixture
    # on the runner's read-only system mount, as installed application code is.
    fixture = (a.dex or a.data / 'data/local/tmp/classes.dex').resolve()
    boot = [f'/apex/com.android.art/javalib/{n}.jar' for n in
            ('core-oj', 'core-libart', 'okhttp', 'bouncycastle', 'apache-xml')]
    boot += ['/apex/com.android.i18n/javalib/core-icu4j.jar',
             '/apex/com.android.conscrypt/javalib/conscrypt.jar']
    if a.framework:
        fragments = [a.sysroot / 'system/etc/classpaths/bootclasspath.pb']
        fragments += sorted(a.sysroot.glob('apex/*/etc/classpaths/bootclasspath.pb'))
        for fragment in fragments:
            for entry in re.findall(rb'/(?:apex|system)/[\w./-]+\.jar', fragment.read_bytes()):
                entry = entry.decode('ascii')
                if entry not in boot:
                    boot.append(entry)
    args = [str(a.runner.resolve()), '--low-memory', '--sysroot', str(a.sysroot.resolve()),
            '--data', str(a.data.resolve()), '--env',
            'LD_LIBRARY_PATH=/apex/com.android.art/lib64:/apex/com.android.os.statsd/lib64:/apex/com.android.runtime/lib64/bionic:/system/lib64',
            '/apex/com.android.art/bin/dalvikvm64', '-Xint', '-Xnoimage-dex2oat',
            '-Ximage:/nonexistent.art', '-Xbootclasspath:' + ':'.join(boot),
            '-cp', '/system/framework/axrb-art-smoke.dex', a.main]
    if a.framework:
        split = args.index('/apex/com.android.art/bin/dalvikvm64')
        args = args[:split] + ['--prop', 'dalvik.vm.usejit=false', '--prop', 'dalvik.vm.usejitprofiles=false',
                '--env', 'BOOTCLASSPATH=' + ':'.join(boot),
                '--env', 'CLASSPATH=/system/framework/axrb-art-smoke.dex',
                '/system/bin/app_process64', '-Xint', '-Xnoimage-dex2oat',
                '-Ximage:/nonexistent.art', '/system/bin', a.main]
    args += a.arg
    if a.check_jni:
        executable = '/system/bin/app_process64' if a.framework else '/apex/com.android.art/bin/dalvikvm64'
        args.insert(args.index(executable) + 1, '-Xcheck:jni')
    if a.host_graphics:
        args[1:1] = ['--host-graphics']
    args[1:1] = ['--mount-ro', '/system/framework/axrb-art-smoke.dex=' + str(fixture)]
    for mount in a.mount_ro:
        args[1:1] = ['--mount-ro', mount]
    for prop in a.prop:
        args[1:1] = ['--prop', prop]
    a.log.parent.mkdir(parents=True, exist_ok=True)
    with a.log.open('wb') as log:
        try:
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=a.timeout, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except subprocess.TimeoutExpired:
            raise SystemExit(f'ART probe timed out; see {a.log}')
    output = a.log.read_text(encoding='utf-8', errors='replace')
    print(output[-6000:])
    if result.returncode or a.expect not in output:
        raise SystemExit(f'ART probe failed ({result.returncode}); see {a.log}')


if __name__ == '__main__':
    main()
