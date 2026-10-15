"""Build a probe APK and verify Direct mode's legacy Oculus driver package.
Developer-only: JDK and Android build tools are needed to compile the fixture.
The probe loads AXRB's runtime the way older Oculus OpenXR loaders do, through
com.oculus.systemdriver, with the Quest identity and controller-only policy on.
"""
import argparse
import re
from pathlib import Path
from probe_app import build_probe, run_probe

PACKAGE = 'com.axrb.legacyprobe'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('direct', 'sysroot', 'work', 'android-jar', 'build-tools'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).resolve().parents[1] / 'tests/art/LegacyDriverProbe.java'
    apk = build_probe(work, args.android_jar, args.build_tools, PACKAGE, 'LegacyDriverProbe', source)
    text, log = run_probe(args.direct, args.sysroot, work, apk, PACKAGE, 'LegacyDriverProbe', 'LEGACY_DRIVER_PROBE',
                          legacy_oculus_driver=args.direct.resolve() / 'guest/axrb-systemdriver.apk',
                          quest_identity=True, controllers_only=True)
    result = re.search(r'LEGACY_DRIVER_PROBE (manufacturer=.*)', text)
    expected = 'manufacturer=Oculus brand=oculus model=Quest 3 gipa=true negotiate=true initialize=true missing=false'
    if not result or result.group(1).strip() != expected:
        raise SystemExit(f'LEGACY_DRIVER_SELFTEST_FAIL: {result.group(1).strip() if result else "no probe result"} ({log})')
    if 'controller-only compatibility=1' not in text:
        raise SystemExit(f'LEGACY_DRIVER_SELFTEST_FAIL: the controller-only policy did not reach the driver ({log})')
    print('LEGACY_DRIVER_SELFTEST_PASS', result.group(1).strip())


if __name__ == '__main__':
    main()
