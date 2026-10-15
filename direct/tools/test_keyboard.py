"""Build a probe APK and verify soft-keyboard requests in a Direct session.
Developer-only: JDK and Android build tools are needed to compile the fixture.
Direct mode has no input method: showing the keyboard must report that it was
not shown, as on a device without one, rather than end the app.
"""
import argparse
import re
from pathlib import Path
from probe_app import build_probe, run_probe

PACKAGE = 'com.axrb.keyboardprobe'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('direct', 'sysroot', 'work', 'android-jar', 'build-tools'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[1] / 'tests/art/KeyboardProbe.java'
    apk = build_probe(work, args.android_jar, args.build_tools, PACKAGE, 'KeyboardProbe', source)
    text, log = run_probe(args.direct, args.sysroot, work, apk, PACKAGE, 'KeyboardProbe', 'KEYBOARD_PROBE')
    result = re.search(r'KEYBOARD_PROBE (shown=\w+ hidden=\w+)', text)
    # Hiding is answered by the app's own InputMethodManager; either result is fine.
    if not result or not result.group(1).startswith('shown=false ') or 'AXRB_SERVICE_UNIMPLEMENTED' in text:
        unimplemented = re.findall(r'AXRB_SERVICE_UNIMPLEMENTED (\S+)', text)
        raise SystemExit(f'KEYBOARD_SELFTEST_FAIL: {result.group(1) if result else "no probe result"} '
                         f'unimplemented={unimplemented} ({log})')
    print('KEYBOARD_SELFTEST_PASS', result.group(1))


if __name__ == '__main__':
    main()
