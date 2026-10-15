"""Build Digitalis's Windows adapter with the NDK's bundled Clang and MSVC CRT.

Clang is needed for the upstream interpreter's 128-bit integers and GNU inline
assembly. The result is a static library: players need no compiler or new DLL.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--ndk', type=Path, required=True)
parser.add_argument('--linker', type=Path, required=True)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--config', default='Release')
parser.add_argument('--heavy', action='store_true')
args = parser.parse_args()
here = Path(__file__).resolve().parent
root = here.parents[1]
source = root / 'third_party/digitalis'
work = args.out.resolve()
work.mkdir(parents=True, exist_ok=True)
linker = args.linker.resolve()
vcvars = linker.parents[6] / 'Auxiliary/Build/vcvars64.bat'
environment = work / 'environment.cmd'
environment.write_text(f'@call "{vcvars}" >nul\n@set\n', encoding='utf-8')
output = subprocess.run(['cmd', '/d', '/c', str(environment)], cwd=work,
                        capture_output=True, text=True, check=True)
env = {k.upper(): v for k, v in os.environ.items()}
for line in output.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key.upper()] = value
if 'INCLUDE' not in env or 'LIB' not in env:
    raise RuntimeError('MSVC x64 development environment could not be initialized')

def run(command):
    subprocess.run([str(x) for x in command], env=env, check=True, cwd=work)

generated = work / 'generated/berberis/assembler'
generated.mkdir(parents=True, exist_ok=True)
run([sys.executable, '-X', 'utf8', source / 'assembler/gen_asm.py', '--binary-assembler',
     generated / 'gen_assembler_x86_32_or_x86_64-inl.h', generated / 'gen_assembler_x86_64-inl.h',
     source / 'assembler/instructions/insn_def_x86.json', source / 'assembler/instructions/insn_def_x86_64.json'])
clang = args.ndk.resolve() / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang-cl.exe'
debug = args.config == 'Debug'
flags = [clang, '--target=x86_64-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc',
         '/MTd' if debug else '/MT', '/Od' if debug else '/O2', '/c', '/utf-8', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS']
includes = [here / 'compat', source, work / 'generated'] + [source / name for name in (
    'base/include', 'guest_state/include', 'guest_state/arm64/include', 'interpreter/include',
    'decoder/include', 'kernel_api/include', 'runtime_primitives/include', 'assembler/include',
    'lite_translator/include', 'lite_translator', 'code_gen_lib/include')]
if args.heavy:
    flags += ['/DAXRB_DIGITALIS_HEAVY']
    includes.append(source / 'heavy_optimizer/arm64/include')
flags += ['/I' + str(path) for path in includes]
objects = []
for name, file in [
    ('engine', here / 'engine.cpp'), ('builtins', here / 'builtins.cpp'),
    ('region', source / 'lite_translator/arm64_to_x86_64/lite_translate_region.cc'),
    ('translator', source / 'lite_translator/arm64_to_x86_64/lite_translator.cc'),
    ('machine', source / 'assembler/machine_code.cc')]:
    obj = work / (name + '.obj')
    run(flags + [file, '/Fo' + str(obj)])
    objects.append(obj)
entry = work / 'entry.obj'
run([linker.parent / 'ml64.exe', '/nologo', '/c', '/Fo' + str(entry), here / 'entry.asm'])
objects.append(entry)
if args.heavy:
    from build_heavy import build
    objects += build(source, work, flags, run, env)
run([linker.parent / 'lib.exe', '/nologo', '/OUT:' + str(work / 'digitalis.lib'), *objects])
run(flags + [here / 'selftest.cpp', '/Fo' + str(work / 'selftest.obj')])
run([linker, '/nologo', '/OUT:' + str(work / 'digitalis-selftest.exe'),
     work / 'selftest.obj', work / 'digitalis.lib'])
run([work / 'digitalis-selftest.exe'])
for option in ('AXRB_DIGITALIS_DISABLE_AVX', 'AXRB_DIGITALIS_NO_CHAIN', 'AXRB_DIGITALIS_DISABLE_EXTENDED_CPU'):
    test_env = dict(env, **{option: '1'})
    subprocess.run([str(work / 'digitalis-selftest.exe')], env=test_env, check=True, cwd=work)
if args.heavy:
    for disable_avx in (False, True):
        test_env = dict(env, AXRB_DIGITALIS_HEAVY='force')
        if disable_avx:
            test_env['AXRB_DIGITALIS_DISABLE_AVX'] = '1'
        subprocess.run([str(work / 'digitalis-selftest.exe')], env=test_env, check=True, cwd=work)
    run(flags + [here / 'differential.cpp', '/Fo' + str(work / 'differential.obj')])
    run([linker, '/nologo', '/OUT:' + str(work / 'digitalis-differential.exe'),
         work / 'differential.obj', work / 'digitalis.lib'])
    run([work / 'digitalis-differential.exe'])
    subprocess.run([str(work / 'digitalis-selftest.exe')],
                   env=dict(env, AXRB_DIGITALIS_HEAVY='0'), check=True, cwd=work)
    # Hot regions optimized on the guest thread instead of in the background.
    subprocess.run([str(work / 'digitalis-selftest.exe')],
                   env=dict(env, AXRB_DIGITALIS_SYNC_HEAVY='1'), check=True, cwd=work)
