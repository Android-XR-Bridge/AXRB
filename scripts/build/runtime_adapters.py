"""Build the two guest-wide compatibility libraries for distribution."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sdk = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'
sys.path.insert(0, str(root / 'scripts/emulator'))
from audio_policy import build_adapter
build_adapter(sdk, root)
compiler = sorted(sdk.glob('ndk/*/toolchains/llvm/prebuilt/windows-x86_64/bin/x86_64-linux-android29-clang++.cmd'),
                  key=lambda p: tuple(int(n) for n in p.parents[5].name.split('.')))[-1]
output = root / 'out/android/vulkan/libVkLayer_AXRB_runtime.so'
output.parent.mkdir(parents=True, exist_ok=True)
subprocess.run([str(compiler), '-std=c++17', '-shared', '-fPIC', '-O2', '-static-libstdc++', '-Wl,-Bsymbolic',
                str(root / 'runtime/vulkan/android_vulkan_layer.cpp'), '-llog', '-o', str(output)], check=True)
