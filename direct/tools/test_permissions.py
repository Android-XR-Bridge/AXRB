"""Build a probe APK and verify real AOSP permission-request callbacks.
Developer-only: JDK and Android build tools are needed to compile the fixture.
The tested runtime itself uses only the bundled Direct runtime.
"""
import argparse
from pathlib import Path
import zipfile
from prepare_art_session import prepare
from test_art import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('direct', 'sysroot', 'work', 'android-jar', 'build-tools'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    data = work / 'data'
    if data.exists():
        parser.error('Use a new work folder for an isolated probe')
    classes, dex = work / 'classes', work / 'dex'
    classes.mkdir(); dex.mkdir()
    source = Path(__file__).resolve().parents[1] / 'tests/art/PermissionProbe.java'
    run(['javac', '--release', '8', '-classpath', args.android_jar, '-d', classes, source])
    run(['java', '-cp', args.build_tools / 'lib/d8.jar', 'com.android.tools.r8.D8', '--min-api', '29', '--lib', args.android_jar, '--output', dex, *classes.rglob('*.class')])
    manifest = work / 'AndroidManifest.xml'
    manifest.write_text('<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.axrb.permissionprobe"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="32"/><uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE"/><uses-permission android:name="android.permission.CAMERA"/><application android:label="Permission probe"><activity android:name=".PermissionProbe" android:exported="true"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter></activity></application></manifest>')
    apk = work / 'permission-probe.apk'
    run([args.build_tools / 'aapt2.exe', 'link', '-I', args.android_jar, '--manifest', manifest, '-o', apk])
    with zipfile.ZipFile(apk, 'a', zipfile.ZIP_DEFLATED) as archive:
        archive.write(dex / 'classes.dex', 'classes.dex')
    direct = args.direct.resolve()
    command = prepare(apk=apk, activity='com.axrb.permissionprobe/com.axrb.permissionprobe.PermissionProbe', sysroot=args.sysroot, data=data,
                      bootstrap=direct / 'art/axrb-bootstrap.dex', native=direct / 'art/lib64', proxies=direct / 'guest/lib64')
    run([direct / 'axrb-direct.exe', *command], log=work / 'permission.log',
        marker='PERMISSION_PROBE_PASS', timeout=120)

if __name__ == '__main__':
    main()
