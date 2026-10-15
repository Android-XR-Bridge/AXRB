"""Build the matched x86 framework bridge and ARM game NDK/audio adapters."""
import argparse
from pathlib import Path
import subprocess
from code_ranges import generate

def build(ndk, output):
    source = Path(__file__).resolve().parents[1]
    tools = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
    output.mkdir(parents=True, exist_ok=True)
    arm = output / 'arm64'
    arm.mkdir(exist_ok=True)
    def run(compiler, target, *args):
        subprocess.run([str(tools / compiler), '--target=' + target,
                        '-O2', '-Wall', '-Werror', '-fPIC', *map(str, args)], check=True)
    for name in ('art_bridge', 'art_media'):
        run('clang.exe', 'x86_64-linux-android33', '-c', source / 'art' / (name+'.c'),
            '-o', output / (name+'.o'))
    library = output / 'libaxrb_art_bridge.so'
    run('clang++.exe', 'x86_64-linux-android33', '-shared', source / 'art/art_binder.cpp',
        output / 'art_bridge.o', output / 'art_media.o', '-landroid', '-ldl',
        '-lbinder_ndk', '-static-libstdc++', '-o', library)
    generate(library)
    run('clang.exe', 'aarch64-linux-android33', '-shared', '-DAXRB_NATIVE_X64_BRIDGE',
        source / 'guest/proxy/libandroid.c', '-o', arm / 'libandroid.so')
    run('clang.exe', 'aarch64-linux-android33', '-shared', source / 'guest/proxy/libOpenSLES.c',
        '-laaudio', '-lm', '-Wl,-soname,libOpenSLES.so', '-o', arm / 'libOpenSLES.so')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ndk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.ndk, args.output)
