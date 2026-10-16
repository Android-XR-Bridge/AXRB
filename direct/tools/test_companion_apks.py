"""Compile/run a Direct companion-package metadata and Java code-loading probe."""
import argparse
from pathlib import Path
from probe_app import build_probe, run_probe

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('direct','sysroot','work','android-jar','build-tools'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--build-only',action='store_true')
    args=parser.parse_args()
    source=Path(__file__).resolve().parents[1]/'tests/art'
    work=args.work.resolve(); work.mkdir(parents=True,exist_ok=True)
    companion=work/'companion'; companion.mkdir(exist_ok=True)
    app=work/'app'; app.mkdir(exist_ok=True)
    helper=build_probe(companion,args.android_jar,args.build_tools,'com.axrb.companionfixture','CompanionHelper',source/'CompanionHelper.java')
    apk=build_probe(app,args.android_jar,args.build_tools,'com.axrb.companionprobe','CompanionApkProbe',source/'CompanionApkProbe.java')
    if args.build_only:
        print('Companion probe APKs compiled; native execution was not requested.')
        return
    text,log=run_probe(args.direct,args.sysroot,app,apk,'com.axrb.companionprobe','CompanionApkProbe',
                       'COMPANION_APK_PROBE',companion_apks=[helper])
    if 'COMPANION_APK_PROBE visible=true context=true class=true main=true' not in text:
        raise SystemExit(f'Companion package runtime probe failed: {log}')
    print('COMPANION_APK_RUNTIME_PROBE_PASS')
if __name__=='__main__': main()
