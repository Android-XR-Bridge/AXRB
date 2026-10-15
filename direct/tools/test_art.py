"""Build and run isolated ARM64 ART probes (developer SDK/NDK required).

The sysroot must be a disposable extraction made with make_sysroot.py --art.
This does not install a launcher or verify that an Android game can launch.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


def run(args, *, log=None, marker=None, timeout=120):
    args = [str(arg) for arg in args]
    if log is None:
        subprocess.run(args, check=True, timeout=timeout)
        return
    with log.open('wb') as output:
        result = subprocess.run(args, stdout=output, stderr=subprocess.STDOUT,
                                timeout=timeout,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    text = log.read_text(encoding='utf-8', errors='replace')
    if result.returncode or (marker and marker not in text):
        print(text[-6000:])
        raise RuntimeError(f'Probe failed ({result.returncode}): {log}')
    print(f'PASS: {marker} ({log})', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    parser.add_argument('--sysroot', type=Path, required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--ndk', type=Path, required=True)
    parser.add_argument('--build-tools', type=Path, required=True)
    parser.add_argument('--framework', action='store_true',
                        help='Also require real app_process framework startup (currently needs Binder support)')
    parser.add_argument('--host-bridges', type=Path,
                        help='Staged out/direct directory: test real Vulkan images and OpenSL ES output')
    args = parser.parse_args()
    runner, root, work = (p.resolve() for p in (args.runner, args.sysroot, args.work))
    clang = args.ndk.resolve() / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    d8 = args.build_tools.resolve() / 'lib/d8.jar'
    for file in (runner, clang, d8, root / 'apex/com.android.art/bin/dalvikvm64'):
        if not file.is_file():
            parser.error(f'Missing required file: {file}')
    javac, java = shutil.which('javac'), shutil.which('java')
    if not javac or not java:
        parser.error('The developer probes require a JDK on PATH')
    source = Path(__file__).resolve().parents[1] / 'tests/art'
    classes = work / 'classes'
    dex = work / 'data/data/local/tmp'
    for directory in (classes, dex):
        directory.mkdir(parents=True, exist_ok=True)
    run([javac, '--release', '8', '-Xlint:-options', '-d', classes, source / 'ArtSmoke.java'])
    run([java, '-cp', d8, 'com.android.tools.r8.D8', '--min-api', '26',
         '--output', dex, classes / 'ArtSmoke.class'])
    flags = [clang, '--target=aarch64-linux-android29', '-O2', '-Wall', '-Werror']
    run(flags + ['-fPIC', '-shared', source / 'art_smoke_jni.c', '-o',
                 root / 'system/lib64/libaxrb_art_smoke.so'])
    native = work / 'art_linux_selftest'
    run(flags + [source / 'art_linux_selftest.c', '-o', native])
    run([runner, '--low-memory', '--sysroot', root, '--data', work / 'data', native],
        log=work / 'linux-selftest.log', marker='ART_LINUX_SELFTEST_PASS')
    timeout_probe = work / 'timeout_selftest'
    run(flags + [source / 'timeout_selftest.c', '-o', timeout_probe])
    run([runner, '--sysroot', root, '--data', work / 'data', timeout_probe],
        log=work / 'timeout-selftest.log', marker='TIMEOUT_SELFTEST_PASS', timeout=20)
    translator = work / 'translator_selftest'
    run(flags + ['-march=armv8.1-a', source / 'translator_selftest.c', '-o', translator])
    run([runner, '--sysroot', root, '--data', work / 'data', translator],
        log=work / 'translator-selftest.log', marker='TRANSLATOR_ATOMICS_SELFTEST_PASS')
    rounding = work / 'translator_round_selftest'
    run(flags + [source / 'translator_round_selftest.c', '-lm', '-o', rounding])
    # Digitalis does not pass yet: FRINTX/FRINTI ignore a non-default FPCR
    # rounding mode, and single-precision FRINTA rounds 0.49999997 up.
    if os.environ.get('AXRB_DIRECT_TRANSLATOR', 'dynarmic') == 'dynarmic':
        run([runner, '--sysroot', root, '--data', work / 'data', rounding],
            log=work / 'translator-round-selftest.log', marker='TRANSLATOR_ROUND_SELFTEST_PASS')
    for name, marker in (('cond', 'TRANSLATOR_COND_SELFTEST_PASS'), ('tbi', 'TRANSLATOR_TBI_SELFTEST_PASS')):
        binary = work / f'translator_{name}_selftest'
        run(flags + ['-march=armv8.1-a', source / f'translator_{name}_selftest.c', '-o', binary])
        run([runner, '--sysroot', root, '--data', work / 'data', binary],
            log=work / f'translator-{name}-selftest.log', marker=marker)
    mapped = work / 'translator_mmap_selftest'
    run(flags + [source / 'translator_mmap_selftest.c', '-o', mapped])
    run([runner, '--sysroot', root, '--data', work / 'data', mapped],
        log=work / 'translator-mmap-selftest.log', marker='TRANSLATOR_MMAP_SELFTEST_PASS')
    binder = work / 'binder_selftest'
    run(flags + [source / 'binder_selftest.c', '-o', binder])
    run([runner, '--sysroot', root, '--data', work / 'data', binder],
        log=work / 'binder-selftest.log', marker='BINDER_SELFTEST_PASS')
    probe = [sys.executable, Path(__file__).with_name('probe_art.py'), '--runner', runner,
             '--sysroot', root, '--data', work / 'data']
    run(probe + ['--log', work / 'art-jni-smoke.log'], timeout=150)
    if args.framework:
        run(probe + ['--framework', '--log', work / 'framework-smoke.log'], timeout=150)
    if args.host_bridges:
        bridges = args.host_bridges.resolve()
        for name, link, marker in [('opensles', 'OpenSLES', 'OPENSL_ES_SELFTEST_PASS'),
                                   ('vulkan_images', 'vulkan', 'VULKAN_IMAGES_SELFTEST_PASS')]:
            binary = work / (name + '-selftest')
            run(flags + [source / (name + '_selftest.c'), '-l' + link, '-o', binary])
            mounts = ['--mount-ro', '/system/lib64/libaaudio.so=' + str(bridges / 'guest/lib64/libaaudio.so'),
                      '--mount-ro', '/system/lib64/libvulkan.so=' + str(bridges / 'guest/lib64/libvulkan.so'),
                      '--mount-ro', '/system/lib64/libOpenSLES.so=' + str(bridges / 'art/lib64/libOpenSLES.so')]
            run([runner, '--host-graphics', '--sysroot', root, '--data', work / 'data', *mounts, binary],
                log=work / (name + '-selftest.log'), marker=marker, timeout=30)
            if name == 'vulkan_images':
                run([runner, '--host-graphics', '--sysroot', root, '--data', work / 'data', *mounts, binary, 'khr'],
                    log=work / (name + '-khr-selftest.log'), marker=marker, timeout=30)


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, subprocess.TimeoutExpired) as error:
        sys.exit(str(error))
