"""Locate verified bundles or a development NDK for runtime adapters."""
import hashlib
import json
import os
from pathlib import Path


def bundled_library(root, relative):
    manifest = root / 'distribution.json'
    if not manifest.exists():
        return None
    expected = json.loads(manifest.read_text(encoding='utf-8'))['sha256'].get(relative)
    library = root / relative
    if not expected or not library.is_file() or hashlib.sha256(library.read_bytes()).hexdigest() != expected:
        raise RuntimeError('Bundled runtime is damaged. Reinstall AXRB.')
    return library


def ndk_compiler(sdk, *, cxx=False):
    driver = 'x86_64-linux-android29-clang' + ('++' if cxx else '') + '.cmd'
    toolchain = Path('toolchains/llvm/prebuilt/windows-x86_64/bin')
    override = os.environ.get('ANDROID_NDK_HOME')
    if override:
        compiler = Path(override) / toolchain / driver
        if not compiler.is_file():
            raise RuntimeError(f'ANDROID_NDK_HOME does not contain the required Windows NDK compiler: {compiler}')
        return compiler

    # The launcher SDK contains the emulator, not necessarily development tools.
    roots = [Path(sdk)]
    for variable in ('ANDROID_HOME', 'ANDROID_SDK_ROOT'):
        value = os.environ.get(variable)
        if value:
            roots.append(Path(value))
    if os.environ.get('LOCALAPPDATA'):
        roots.append(Path(os.environ['LOCALAPPDATA']) / 'Android/Sdk')
    for root in dict.fromkeys(roots):
        compilers = [p for p in root.glob(f'ndk/*/{toolchain.as_posix()}/{driver}')
                     if p.is_file() and all(n.isdigit() for n in p.parents[5].name.split('.'))]
        if compilers:
            return max(compilers, key=lambda p: tuple(int(n) for n in p.parents[5].name.split('.')))
    raise RuntimeError('Android development NDK required for runtime adapters. '
                       'Install it with scripts/build/android_sdk.ps1 after reviewing the SDK license, '
                       'or set ANDROID_NDK_HOME to an installed NDK.')
