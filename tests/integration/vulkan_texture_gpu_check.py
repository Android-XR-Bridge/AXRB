"""Build and run the real Vulkan texture staging regression on an emulator."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'scripts/emulator'))
from distribution import ndk_compiler

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, default=Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk')
parser.add_argument('--serial', default='emulator-5584')
args = parser.parse_args()
output = root / 'out/android/vulkan/texture_gpu_smoke'
output.parent.mkdir(parents=True, exist_ok=True)
objects = [str(root / 'runtime/vulkan/texture/bc7e' / name)
           for name in ('bc7e.o', 'bc7e_sse4.o', 'bc7e_avx2.o')]
subprocess.run([str(ndk_compiler(args.sdk, cxx=True)), '-std=c++17', '-O2', '-static-libstdc++',
                str(root / 'tests/android/texture_gpu_smoke.cpp'), *objects, '-llog', '-lvulkan',
                '-o', str(output)], check=True, timeout=120)
adb = [str(args.sdk / 'platform-tools/adb.exe'), '-s', args.serial]
def run(*parts):
    return subprocess.run([*adb, *parts], check=True, text=True, capture_output=True, timeout=90).stdout.strip()

remote = '/data/local/tmp/axrb_texture_gpu_smoke'
original = run('shell', 'getprop debug.axrb.astc_gpu')
run('push', str(output), remote)
run('shell', 'chmod 755 ' + remote)
try:
    run('shell', 'setprop debug.axrb.astc_gpu 1')
    print(run('shell', 'timeout 60 ' + remote))
finally:
    run('shell', 'setprop debug.axrb.astc_gpu ' + shlex.quote(original))
