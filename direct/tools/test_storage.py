"""Build a probe APK and verify real AOSP saves across process restarts.
Developer-only: JDK and Android build tools are needed to compile the fixture.
The tested runtime itself uses only the bundled Direct runtime.
"""
import argparse
import ctypes
import os
import time
import uuid
import json
from pathlib import Path
import subprocess
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
        parser.error('Use a new work folder so the write test starts with empty app storage')
    classes, dex = work / 'classes', work / 'dex'
    classes.mkdir(); dex.mkdir()
    source = Path(__file__).resolve().parents[1] / 'tests/art/StorageProbe.java'
    run(['javac', '--release', '8', '-classpath', args.android_jar, '-d', classes, source])
    run(['java', '-cp', args.build_tools / 'lib/d8.jar', 'com.android.tools.r8.D8', '--min-api', '29', '--lib', args.android_jar, '--output', dex, *classes.rglob('*.class')])
    manifest = work / 'AndroidManifest.xml'
    manifest.write_text('<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.axrb.storageprobe"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="32"/><application android:label="Storage probe"><activity android:name=".StorageProbe" android:exported="true"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter></activity></application></manifest>')
    apk = work / 'storage-probe.apk'
    run([args.build_tools / 'aapt2.exe', 'link', '-I', args.android_jar, '--manifest', manifest, '-o', apk])
    with zipfile.ZipFile(apk, 'a', zipfile.ZIP_DEFLATED) as archive:
        archive.write(dex / 'classes.dex', 'classes.dex')
    direct = args.direct.resolve()
    command = prepare(apk=apk, activity='com.axrb.storageprobe/com.axrb.storageprobe.StorageProbe', sysroot=args.sysroot, data=data,
                      bootstrap=direct / 'art/axrb-bootstrap.dex', native=direct / 'art/lib64', proxies=direct / 'guest/lib64')
    for phase in ('write', 'stop', 'read'):
        if phase == 'stop':
            # Exercise exactly the named stop event used by run_direct_game.ps1.
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.CreateEventW.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_wchar_p]
            kernel.CreateEventW.restype = ctypes.c_void_p
            kernel.SetEvent.argtypes = [ctypes.c_void_p]
            kernel.CloseHandle.argtypes = [ctypes.c_void_p]
            event_name = 'Local\\AXRB.DirectStop.' + uuid.uuid4().hex
            event = kernel.CreateEventW(None, 1, 0, event_name)
            if not event:
                raise ctypes.WinError(ctypes.get_last_error())
            log = work / 'stop.log'
            child = None
            try:
                with log.open('wb') as output:
                    child = subprocess.Popen([str(direct / 'axrb-direct.exe'), '--env', 'AXRB_STORAGE_TEST_PHASE=stop', *map(str, command)],
                        env={**os.environ, 'AXRB_DIRECT_STOP_EVENT': event_name}, stdout=output, stderr=subprocess.STDOUT,
                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                    deadline = time.monotonic() + 240
                    while 'STORAGE_PROBE_READY_FOR_STOP' not in log.read_text(encoding='utf-8', errors='replace'):
                        if child.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError('Storage lifecycle startup failed: ' + str(log))
                        time.sleep(0.1)
                    if not kernel.SetEvent(event):
                        raise ctypes.WinError(ctypes.get_last_error())
                    assert child.wait(timeout=25) == 0, log
                transcript = log.read_text(encoding='utf-8', errors='replace')
                assert 'STORAGE_PROBE_ASYNC_SAVE_QUEUED' in transcript and 'AXRB_AOSP_SESSION_CLOSED' in transcript, transcript[-2000:]
                print('PASS: graceful onStop and session shutdown', flush=True)
            finally:
                if child and child.poll() is None:
                    child.kill(); child.wait()
                kernel.CloseHandle(event)
            continue
        run([direct / 'axrb-direct.exe', '--env', 'AXRB_STORAGE_TEST_PHASE=' + phase, *command],
            log=work / (phase + '.log'), marker='STORAGE_PROBE_' + phase.upper() + '_PASS', timeout=240)
    saves = list(data.rglob('save.json')) + list(data.rglob('external-save.txt'))
    assert len(saves) == 2, saves
    for save in saves:
        assert save.read_text(encoding='utf-8') == 'AXRB save: \u00e4 \u65e5\u672c 12345'
    report = {'passed': True, 'tests': ['private file', 'UTF-8', 'fsync', 'rename', 'SharedPreferences', 'onStop asynchronous apply and graceful shutdown', 'SQLite WAL transaction', 'external app file', 'cache write', 'delete', 'process restart', 'Windows file verification'], 'windowsSaves': [str(p) for p in saves]}
    (work / 'report.json').write_text(json.dumps(report, indent=2))
    print('STORAGE_PERSISTENCE_PASS', flush=True)


if __name__ == '__main__':
    main()
