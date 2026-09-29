"""Regenerate the prebuilt bc7e objects in runtime/vulkan/texture/bc7e.

bc7e.ispc is written in Intel's ISPC language. The Vulkan layer links the
objects ISPC makes from it, checked in beside the source, so building AXRB
does not need ISPC. Run this after changing bc7e.ispc: it downloads the pinned
ISPC release into out/tools (checking its SHA-256) and compiles the SSE4 and
AVX2 variants for Android x86_64, with ISPC's runtime dispatch between them.
"""
import hashlib
from pathlib import Path
import subprocess
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = '1.31.0'
ARCHIVE = f'ispc-v{VERSION}-windows.zip'
URL = f'https://github.com/ispc/ispc/releases/download/v{VERSION}/{ARCHIVE}'
SHA256 = '9a18793800b91d5be7b851513672cd9a81a985a5a5dfec5611c2318e8ad4140a'
SOURCE = ROOT / 'runtime/vulkan/texture/bc7e'


def ispc():
    tools = ROOT / 'out/tools'
    exe = tools / f'ispc-v{VERSION}-windows/bin/ispc.exe'
    if exe.exists():
        return exe
    tools.mkdir(parents=True, exist_ok=True)
    archive = tools / ARCHIVE
    if not archive.exists():
        print('downloading', URL)
        urllib.request.urlretrieve(URL, archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != SHA256:
        archive.unlink()
        raise SystemExit(f'{ARCHIVE}: SHA-256 {digest}, expected {SHA256}')
    with zipfile.ZipFile(archive) as z:
        z.extractall(tools)
    return exe


def main():
    subprocess.run([str(ispc()), '-O2', '--target=sse4-i32x4,avx2-i32x8', '--target-os=android', '--arch=x86-64', '--pic',
                    '--opt=disable-assertions', '--woff', '-o', str(SOURCE / 'bc7e.o'), '-h', str(SOURCE / 'bc7e_ispc.h'),
                    str(SOURCE / 'bc7e.ispc')], check=True)
    # Only the dispatching header is used; ISPC also writes one per target.
    for extra in ('bc7e_ispc_sse4.h', 'bc7e_ispc_avx2.h'):
        (SOURCE / extra).unlink(missing_ok=True)
    for name in ('bc7e.o', 'bc7e_sse4.o', 'bc7e_avx2.o'):
        print(name, hashlib.sha256((SOURCE / name).read_bytes()).hexdigest())


if __name__ == '__main__':
    main()
