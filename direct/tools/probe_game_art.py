"""Stage an APK into an isolated real-ART session and capture its startup log.

Activity creation is only a diagnostic milestone, not a successful game test.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('runner', 'sysroot', 'data', 'bootstrap', 'apk', 'log'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--activity', required=True)
    p.add_argument('--assets', type=Path)
    p.add_argument('--timeout', type=int, default=60)
    p.add_argument('--proxies', type=Path, required=True)
    p.add_argument('--native', type=Path, required=True)
    p.add_argument('--runtime', type=Path)
    p.add_argument('--check-jni', action='store_true')
    a = p.parse_args()
    from prepare_art_session import prepare
    args = prepare(apk=a.apk, activity=a.activity, sysroot=a.sysroot, data=a.data,
                   bootstrap=a.bootstrap, native=a.native, proxies=a.proxies,
                   runtime=a.runtime, assets=a.assets, check_jni=a.check_jni,
                   diagnostics=True)
    a.log.parent.mkdir(parents=True, exist_ok=True)
    with a.log.open('wb') as log:
        try:
            result = subprocess.run([str(a.runner.resolve()), *args], stdout=log,
                                    stderr=subprocess.STDOUT, timeout=a.timeout,
                                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except subprocess.TimeoutExpired:
            raise SystemExit(f'ART probe timed out; see {a.log}')
    output = a.log.read_text(encoding='utf-8', errors='replace')
    print(output[-6000:])
    if result.returncode or 'AXRB_AOSP_ACTIVITY_CREATED' not in output:
        raise SystemExit(f'ART probe failed ({result.returncode}); see {a.log}')



if __name__ == '__main__':
    main()
