"""Preserve source license notices for Digitalis and its bundled libraries."""
import argparse
import os
from pathlib import Path
import shutil


def collect(aosp, bundle):
    # Include the complete notice sets of the guest/host library source projects.
    projects = (
        'digitalis', 'build/soong/licenses', 'bionic',
        'frameworks/libs/binary_translation', 'frameworks/libs/native_bridge_support',
        'frameworks/native', 'system/libbase', 'system/core', 'system/logging',
        'libnativehelper', 'external/libcxx', 'external/libcxxabi',
        'external/compiler-rt', 'external/boringssl', 'external/zlib',
        'external/sqlite', 'external/icu', 'external/fmtlib',
        'prebuilts/clang/host/linux-x86/clang-r563880c',
    )
    for project in projects:
        source = aosp / project
        if not source.is_dir():
            continue
        for directory, dirs, files in os.walk(source):
            dirs[:] = [d for d in dirs if d not in ('.git', 'out', 'dist')]
            for name in files:
                stem = name.upper().split('.')[0]
                if stem not in ('LICENSE', 'LICENCE', 'NOTICE', 'COPYING'):
                    continue
                file = Path(directory) / name
                if not file.is_file():
                    continue
                target = bundle / 'licenses' / file.relative_to(aosp)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
    shutil.copyfile(aosp / 'digitalis/LICENSE', bundle / 'LICENSE')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('aosp', type=Path)
    parser.add_argument('bundle', type=Path)
    args = parser.parse_args()
    collect(args.aosp.resolve(), args.bundle.resolve())
