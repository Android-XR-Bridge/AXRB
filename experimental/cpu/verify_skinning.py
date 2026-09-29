"""Prove the native Unity skinning kernels are bit-identical to Unity's own code.

Builds tests/native/unity_skinning_equivalence.cpp for arm64 (runs Unity's
original machine code through the emulator's translator) and x86_64 (runs the
replacement), executes both on the running test emulator and compares output.
"""
import os
from pathlib import Path
import subprocess
import tempfile

from session import ADB, ROOT, SERIAL

NDK = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/windows-x86_64/bin'


def adb(*args):
    return subprocess.run([ADB, '-P', '5038', '-s', SERIAL, *args], check=True, capture_output=True, text=True).stdout


def main():
    source = ROOT / 'tests/native/unity_skinning_equivalence.cpp'
    with tempfile.TemporaryDirectory() as work:
        outputs = {}
        for arch in ['aarch64', 'x86_64']:
            binary = Path(work) / f'skin_{arch}'
            subprocess.run([str(NDK / 'clang++.exe'), f'--target={arch}-linux-android30', '-std=c++20', '-O2',
                            '-static-libstdc++', str(source), '-o', str(binary)], check=True)
            remote = f'/data/local/tmp/{binary.name}'
            adb('push', str(binary), remote)
            print(adb('shell', f'chmod 755 {remote} && {remote} {remote}.bin | tail -1').strip(), arch)
            outputs[arch] = adb('shell', f'sha256sum {remote}.bin').split()[0]
    print(outputs)
    if len(set(outputs.values())) != 1:
        raise SystemExit('MISMATCH: native kernels differ from Unity code')
    print('IDENTICAL')


if __name__ == '__main__':
    main()
