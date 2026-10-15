"""Build the real-ART host bridge and matched AOSP NDK forwarding adapters."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import shutil


def run(args):
    subprocess.run(list(map(str, args)), check=True)


def exports(nm, path):
    """(name, version, is_function) for each defined dynamic export."""
    output = subprocess.check_output([str(nm), '-D', '--defined-only', str(path)], text=True)
    result = []
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 3 or parts[1] not in ('T', 'W', 'D', 'B', 'R', 'V'):
            continue
        name, *version = parts[-1].split('@@')
        # Control-flow-integrity bookkeeping, not API: the linker requires
        # __cfi_check to be page-aligned code of the library itself.
        if name.startswith('__cfi'):
            continue
        result.append((name, version[0] if version else None, parts[1] in ('T', 'W')))
    return result


def rename_soname(original, expected, replacement):
    """The library with only its SONAME string changed (same length or shorter)."""
    data = bytearray(original.read_bytes())
    if data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected an ELF64 little-endian AOSP NDK library')
    if len(replacement) > len(expected):
        raise ValueError('The replacement SONAME must not be longer than the original')
    offset = struct.unpack_from('<Q', data, 40)[0]
    entry_size, count = struct.unpack_from('<HH', data, 58)
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * entry_size) for i in range(count)]
    changed = False
    for section in sections:
        if section[1] != 6:
            continue
        strings = sections[section[6]][4]
        for pos in range(section[4], section[4] + section[5], 16):
            tag, value = struct.unpack_from('<QQ', data, pos)
            if tag == 14:
                begin = strings + value
                end = data.index(0, begin)
                old = bytes(data[begin:end])
                if old != expected:
                    raise ValueError(f'Unexpected original SONAME {old!r}')
                data[begin:end] = replacement.ljust(end - begin, b'\0')
                changed = True
    if not changed:
        raise ValueError('AOSP NDK library has no SONAME')
    return bytes(data)


def forwarding_proxy(tools, flags, original, renamed, overrides_object, output, libs, default_version):
    """Builds `output` under the original SONAME: the functions defined in
    overrides_object, and every other export forwarded to the AOSP library,
    which is written beside it under the SONAME `renamed`."""
    clang, cxx, nm = tools
    soname = original.name
    work = output.parent / output.stem
    (output.parent / renamed).write_bytes(rename_soname(original, soname.encode(), renamed.encode()))
    defined = subprocess.check_output([str(nm), '--defined-only', str(overrides_object)], text=True)
    overrides = {line.split()[-1] for line in defined.splitlines() if len(line.split()) >= 3}
    symbols = exports(nm, original)
    functions = [name for name, _, function in symbols if function and name not in overrides]
    data = [name for name, _, function in symbols if not function and name not in overrides]
    for name in functions + data:
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', name):
            raise ValueError('Unsupported ELF export ' + name)
    assembly = ['.text']
    loader = ['#include <dlfcn.h>', '#include <stdio.h>', '#include <stdlib.h>']
    for i, name in enumerate(functions):
        assembly += [f'.global {name}', f'.type {name}, %function', f'{name}:',
                     f'adrp x16, axrb_forward_{i}', f'ldr x16, [x16, :lo12:axrb_forward_{i}]', 'br x16', f'.size {name}, .-{name}']
        loader += [f'__attribute__((visibility("hidden"))) void* axrb_forward_{i};']
    # Data exports (pointer-sized constants such as AMEDIAFORMAT_KEY_*) are
    # copied from the loaded original before any dependent library runs.
    loader += [f'void* {name};' for name in data]
    loader += ['__attribute__((constructor)) static void initialize(void) {',
               f'void* library = dlopen("/system/lib64/{renamed}", RTLD_NOW | RTLD_LOCAL);',
               f'if (!library) {{ fprintf(stderr, "AOSP {soname} forwarding library: %s\\n", dlerror()); abort(); }}']
    if data:
        loader += ['void** value;']
    for i, name in enumerate(functions):
        loader += [f'axrb_forward_{i} = dlsym(library, "{name}");',
                   f'if (!axrb_forward_{i}) {{ fprintf(stderr, "Missing AOSP export: {name}\\n"); abort(); }}']
    for name in data:
        loader += [f'value = (void**)dlsym(library, "{name}");',
                   f'if (!value) {{ fprintf(stderr, "Missing AOSP export: {name}\\n"); abort(); }}',
                   f'{name} = *value;']
    loader += ['}']
    versions = {}
    for name, version, _ in symbols:
        versions.setdefault(version or default_version, []).append(name)
    Path(f'{work}_forward.S').write_text('\n'.join(assembly) + '\n')
    Path(f'{work}_forward.c').write_text('\n'.join(loader) + '\n')
    Path(f'{work}.map').write_text('\n'.join(name + ' { global: ' + '; '.join(names) + '; local: *; };' for name, names in versions.items()))
    run([clang, *flags, '-c', f'{work}_forward.c', '-o', f'{work}_forward.o'])
    run([cxx, *flags, '-shared', overrides_object, f'{work}_forward.o', f'{work}_forward.S',
         '-Wl,-soname,' + soname, f'-Wl,--version-script={work}.map',
         '-L' + str(output.parent), '-Wl,--no-as-needed', '-l:' + renamed, *libs, '-ldl', '-static-libstdc++', '-o', output])


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('ndk', 'sysroot', 'output'):
        p.add_argument('--' + name, type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    bin = a.ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
    clang, cxx, nm = [bin / name for name in ('clang.exe', 'clang++.exe', 'llvm-nm.exe')]
    source = Path(__file__).resolve().parents[1] / 'art'
    flags = ['--target=aarch64-linux-android33', '-O2', '-Wall', '-Werror', '-fPIC']
    run([clang, *flags, '-shared', source.parent / 'guest/proxy/libOpenSLES.c',
         '-laaudio', '-lm', '-Wl,-soname,libOpenSLES.so', '-o', a.output / 'libOpenSLES.so'])
    run([clang, *flags, '-c', source / 'art_bridge.c', '-o', a.output / 'art_bridge.o'])
    run([clang, *flags, '-c', source / 'art_media.c', '-o', a.output / 'art_media.o'])
    run([cxx, *flags, '-shared', source / 'art_binder.cpp', a.output / 'art_bridge.o', a.output / 'art_media.o',
         '-landroid', '-ldl', '-lbinder_ndk', '-static-libstdc++', '-o', a.output / 'libaxrb_art_bridge.so'])
    shutil.copyfile(a.sysroot / 'system/etc/NOTICE.xml.gz', a.output / 'AOSP-NOTICE.xml.gz')
    tools = (clang, cxx, nm)
    # The matched AOSP libraries keep their asset, input, window, JNI and
    # AMediaFormat implementations; only the overridden functions change.
    run([cxx, *flags, '-Wno-deprecated-declarations', '-c', source / 'ndk_host.cpp', '-o', a.output / 'ndk_host.o'])
    forwarding_proxy(tools, flags, a.sysroot / 'system/lib64/libandroid.so', 'libaxrbndk.so', a.output / 'ndk_host.o',
                     a.output / 'libandroid.so', [], 'LIBANDROID')
    run([cxx, *flags, '-c', source / 'ndk_media.cpp', '-o', a.output / 'ndk_media.o'])
    forwarding_proxy(tools, flags, a.sysroot / 'system/lib64/libmediandk.so', 'libaxrbmndk.so', a.output / 'ndk_media.o',
                     a.output / 'libmediandk.so', ['-llog'], 'LIBMEDIANDK')
    print(a.output)


if __name__ == '__main__':
    main()
