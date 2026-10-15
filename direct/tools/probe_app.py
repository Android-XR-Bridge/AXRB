"""Build a one-activity probe APK and run it in a Direct session (developer-only).
JDK and Android build tools are needed to compile the fixture; the run itself
uses only the staged Direct runtime.
"""
import subprocess
import time
import zipfile
from pathlib import Path
from prepare_art_session import prepare
from test_art import run


def build_probe(work, android_jar, build_tools, package, activity, source):
    """Compiles source (one Activity) into <work>/<package>.apk."""
    classes, dex = work / 'classes', work / 'dex'
    classes.mkdir(parents=True, exist_ok=True); dex.mkdir(exist_ok=True)
    run(['javac', '--release', '8', '-classpath', android_jar, '-d', classes, source])
    run(['java', '-cp', build_tools / 'lib/d8.jar', 'com.android.tools.r8.D8', '--min-api', '29', '--lib', android_jar,
         '--output', dex, *classes.rglob('*.class')])
    manifest = work / 'AndroidManifest.xml'
    manifest.write_text(f'<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="{package}"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="29"/><application android:label="{activity}"><activity android:name=".{activity}" android:exported="true"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter></activity></application></manifest>')
    apk = work / (package + '.apk')
    run([build_tools / 'aapt2.exe', 'link', '-I', android_jar, '--manifest', manifest, '-o', apk])
    with zipfile.ZipFile(apk, 'a', zipfile.ZIP_DEFLATED) as archive:
        archive.write(dex / 'classes.dex', 'classes.dex')
    return apk


def run_probe(direct, sysroot, work, apk, package, activity, marker, **options):
    """Runs the probe until it prints marker (plus a moment for late log lines); returns its log text."""
    direct = Path(direct).resolve()
    command = prepare(apk=apk, activity=f'{package}/{package}.{activity}', sysroot=sysroot, data=work / 'data',
                      bootstrap=direct / 'art/axrb-bootstrap.dex', native=direct / 'art/lib64', proxies=direct / 'guest/lib64', **options)
    log = work / (activity + '.log')
    with log.open('wb') as output:
        child = subprocess.Popen([str(direct / 'axrb-direct.exe'), *map(str, command)], stdout=output, stderr=subprocess.STDOUT,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        deadline = time.monotonic() + 240
        while child.poll() is None and time.monotonic() < deadline:
            if marker in log.read_text(encoding='utf-8', errors='replace'):
                time.sleep(2)
                break
            time.sleep(0.2)
        if child.poll() is None:
            child.kill()
            child.wait()
    return log.read_text(encoding='utf-8', errors='replace'), log
