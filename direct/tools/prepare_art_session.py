"""Prepare a Direct-mode AOSP application without an installed Android SDK.

Only Python's standard library is used. The launcher bundles the bootstrap DEX,
native bridges and Python; the matched AOSP image is installed once at setup.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import zipfile


def required(path):
    path = Path(path).resolve()
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f'Direct mode installation is incomplete: {path}')
    return path


def stage_libraries(apk, data):
    # Separate APKs and revisions, including libraries with unchanged sizes.
    # ZIP CRCs and sizes avoid hashing multi-gigabyte game assets at each Play.
    with zipfile.ZipFile(apk) as archive:
        entries = [e for e in archive.infolist()
                   if e.filename.startswith('lib/arm64-v8a/') and not e.is_dir()]
        manifest = [(e.filename, e.CRC, e.file_size) for e in entries]
        identity = hashlib.sha256(json.dumps([str(apk), manifest]).encode()).hexdigest()
        libraries = data / 'installed' / identity / 'lib' / 'arm64'
        receipt = libraries / '.complete.json'
        if receipt.is_file() and all((libraries / Path(e.filename).name).is_file()
                and (libraries / Path(e.filename).name).stat().st_size == e.file_size for e in entries):
            return libraries
        libraries.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='extract-', dir=libraries.parent) as temporary:
            staging = Path(temporary)
            names = set()
            for entry in entries:
                name = entry.filename[len('lib/arm64-v8a/'):]
                if not re.fullmatch(r'[A-Za-z0-9_.+-]+', name) or name in ('.', '..') or name.casefold() in names:
                    raise ValueError(f'Unsafe or duplicate library path: {entry.filename}')
                names.add(name.casefold())
                with archive.open(entry) as source, (staging / name).open('wb') as target:
                    shutil.copyfileobj(source, target)
            libraries.mkdir(exist_ok=True)
            for source in staging.iterdir():
                os.replace(source, libraries / source.name)
            receipt.write_text(json.dumps(manifest), encoding='utf-8')
        return libraries


def boot_classpath(sysroot):
    boot = [f'/apex/com.android.art/javalib/{n}.jar' for n in
            ('core-oj', 'core-libart', 'okhttp', 'bouncycastle', 'apache-xml')]
    boot += ['/apex/com.android.i18n/javalib/core-icu4j.jar',
             '/apex/com.android.conscrypt/javalib/conscrypt.jar']
    fragments = [required(sysroot / 'system/etc/classpaths/bootclasspath.pb')]
    fragments += sorted(sysroot.glob('apex/*/etc/classpaths/bootclasspath.pb'))
    for fragment in fragments:
        for value in re.findall(rb'/(?:apex|system)/[\w./-]+\.jar', fragment.read_bytes()):
            value = value.decode('ascii')
            if value not in boot:
                boot.append(value)
    for value in boot:
        required(sysroot / value.lstrip('/'))
    return ':'.join(boot)


# What Unity's older Oculus hardware check and Oculus loaders read on a Quest.
QUEST_IDENTITY = {'manufacturer': 'Oculus', 'brand': 'oculus', 'model': 'Quest 3'}
LEGACY_DRIVER_ROOT = '/data/app/com.oculus.systemdriver-axrb'


def prepare(*, apk, activity, sysroot, data, bootstrap, native, proxies,
            runtime=None, assets=None, check_jni=False, diagnostics=False,
            legacy_oculus_driver=None, quest_identity=False, controllers_only=False, companion_apks=None):
    apk, bootstrap = required(apk), required(bootstrap)
    sysroot, data = Path(sysroot).resolve(), Path(data).resolve()
    native, proxies = Path(native), Path(proxies)
    required(sysroot / 'system/bin/app_process64')
    app_root = '/data/app/axrb'
    if '/' in activity:
        package, activity = activity.split('/', 1)
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*', package):
            raise ValueError('Invalid application package')
        # Native loaders can locate their APK by its package name, as on Android.
        app_root = '/data/app/' + package + '-axrb'
        if activity.startswith('.'):
            activity = package + activity
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.$]*', activity):
        raise ValueError('A fully qualified launch activity is required')
    boot = boot_classpath(sysroot)
    libraries = stage_libraries(apk, data)
    args = ['--host-graphics', '--low-memory', '--sysroot', str(sysroot), '--data', str(data)]

    def mount(guest, host, directory=False):
        host = Path(host).resolve() if directory else required(host)
        args.extend(['--mount-ro', guest + '=' + str(host)])

    mount('/system/framework/axrb-bootstrap.dex', bootstrap)
    mount(app_root + '/base.apk', apk)
    mount(app_root + '/lib/arm64', libraries, True)
    for name in ('libvulkan.so', 'libaaudio.so'):
        mount('/system/lib64/' + name, proxies / name)
    for name in ('libandroid.so', 'libaxrbndk.so', 'libmediandk.so', 'libaxrbmndk.so', 'libaxrb_art_bridge.so', 'libOpenSLES.so'):
        mount('/system/lib64/' + name, native / name)
    for name in ('EGL', 'GLESv1_CM', 'GLESv2'):
        mount('/vendor/lib64/egl/lib' + name + '_axrb.so', proxies / ('lib' + name + '.so'))
    mount('/data/app/axrb/lib/arm64/libopenxr_runtime.so', runtime or proxies / 'libopenxr_runtime.so')
    if app_root != '/data/app/axrb':
        mount(app_root + '/lib/arm64/libopenxr_runtime.so', runtime or proxies / 'libopenxr_runtime.so')
    if assets:
        for line in Path(assets).read_text(encoding='utf-8-sig').splitlines():
            if not line.strip():
                continue
            guest, host = line.split('\t', 1)
            if not guest.startswith(('Android/obb/', 'Android/data/')) or any(
                    part in ('', '.', '..') for part in guest.split('/')) or '\\' in guest:
                raise ValueError(f'Asset path outside app storage: {guest}')
            mount('/storage/emulated/0/' + guest, host)
    if legacy_oculus_driver:
        # Older Oculus loaders load com.oculus.systemdriver's DriverLoader
        # instead of asking the OpenXR runtime broker. The bootstrap reports
        # the package as installed when this property names its APK.
        driver = required(legacy_oculus_driver)
        mount(LEGACY_DRIVER_ROOT + '/base.apk', driver)
        mount(LEGACY_DRIVER_ROOT + '/lib/arm64', stage_libraries(driver, data), True)
        # The session's runtime, like the game's, even if the APK carries another build.
        mount(LEGACY_DRIVER_ROOT + '/lib/arm64/libopenxr_runtime.so', runtime or proxies / 'libopenxr_runtime.so')
        args.extend(['--prop', 'ro.axrb.legacy_oculus_driver=' + LEGACY_DRIVER_ROOT + '/base.apk'])
    companions = [required(path) for path in (companion_apks or [])]
    if len(companions) > 8:
        raise ValueError('At most eight companion APKs are supported')
    if len(set(companions)) != len(companions) or apk in companions or (legacy_oculus_driver and Path(legacy_oculus_driver).resolve() in companions):
        raise ValueError('Companion APKs must be distinct from each other, the game and the legacy driver')
    companion_paths = []
    for index, companion in enumerate(companions):
        guest = f'/data/app/axrb-companion-{index}'
        mount(guest + '/base.apk', companion)
        mount(guest + '/lib/arm64', stage_libraries(companion, data), True)
        companion_paths.append(guest + '/base.apk')
    if companion_paths:
        args.extend(['--prop', 'ro.axrb.companion_count=' + str(len(companion_paths))])
        for index, path in enumerate(companion_paths):
            args.extend(['--prop', f'ro.axrb.companion_apk.{index}=' + path])
    if controllers_only:
        if not legacy_oculus_driver:
            raise ValueError('The controller-only policy is applied by the legacy Oculus driver')
        if app_root == '/data/app/axrb':
            raise ValueError('The controller-only policy needs the application package')
        args.extend(['--prop', f'persist.sys.axrb.legacy.controllers_only.{package}=1'])
    if quest_identity:
        for key, value in QUEST_IDENTITY.items():
            for scope in ('', 'system.', 'vendor.', 'product.', 'odm.', 'system_ext.'):
                args.extend(['--prop', f'ro.product.{scope}{key}={value}'])
    for prop in ('ro.hardware.egl=axrb', 'servicemanager.ready=true',
                 'dalvik.vm.usejit=false', 'dalvik.vm.usejitprofiles=false'):
        args.extend(['--prop', prop])
    for env in ('LD_LIBRARY_PATH=/apex/com.android.art/lib64:/apex/com.android.os.statsd/lib64:/apex/com.android.runtime/lib64/bionic:/system/lib64',
                'BOOTCLASSPATH=' + boot, 'CLASSPATH=/system/framework/axrb-bootstrap.dex'):
        args.extend(['--env', env])
    args += ['/system/bin/app_process64', '-Xint', '-Xnoimage-dex2oat', '-Ximage:/nonexistent.art']
    if check_jni:
        args += ['-Xcheck:jni']
    args += ['/system/bin', 'com.axrb.runtime.AppProcess', app_root + '/base.apk', activity]
    if diagnostics:
        args += ['--diagnostics']
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('apk', 'sysroot', 'data', 'bootstrap', 'native', 'proxies'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--activity', required=True)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--assets', type=Path)
    parser.add_argument('--check-jni', action='store_true')
    parser.add_argument('--legacy-oculus-driver', type=Path)
    parser.add_argument('--quest-identity', action='store_true')
    parser.add_argument('--controllers-only', action='store_true')
    parser.add_argument('--companion-apk', dest='companion_apks', type=Path, action='append')
    parser.add_argument('--diagnostics', action='store_true')
    print(json.dumps(prepare(**vars(parser.parse_args())), ensure_ascii=True))


if __name__ == '__main__':
    main()
