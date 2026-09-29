"""Build the two guest-wide compatibility libraries for distribution."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sdk = Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk'
sys.path.insert(0, str(root / 'scripts/emulator'))
from audio_policy import build_adapter
from distribution import ndk_compiler
build_adapter(sdk, root)
compiler = ndk_compiler(sdk, cxx=True)
output = root / 'out/android/vulkan/libVkLayer_AXRB_runtime.so'
output.parent.mkdir(parents=True, exist_ok=True)
bc7e = [str(root / 'runtime/vulkan/texture/bc7e' / name) for name in ('bc7e.o', 'bc7e_sse4.o', 'bc7e_avx2.o')]
subprocess.run([str(compiler), '-std=c++17', '-shared', '-fPIC', '-O2', '-static-libstdc++', '-Wl,-Bsymbolic',
                str(root / 'runtime/vulkan/android_vulkan_layer.cpp'), *bc7e, '-llog', '-o', str(output)], check=True)
