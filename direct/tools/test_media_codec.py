"""Build a probe APK and verify AMediaExtractor/AMediaCodec on the host's Media
Foundation backend: an H.264 + AAC MP4 asset, decoded synchronously and with
asynchronous callbacks. Developer-only: JDK, Android build tools, the NDK and
ffmpeg (for the fixture) are needed. The tested runtime is the staged out/direct.
"""
import argparse
from pathlib import Path
import zipfile
from prepare_art_session import prepare
from test_art import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('direct', 'sysroot', 'work', 'android-jar', 'build-tools', 'ndk', 'ffmpeg'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    data = work / 'data'
    if data.exists():
        parser.error('Use a new work folder for an isolated probe')
    tests = Path(__file__).resolve().parents[1] / 'tests/art'
    # One second: 30 frames of 320x240 H.264 and 44.1 kHz stereo AAC.
    clip = work / 'probe.mp4'
    run([args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc=size=320x240:rate=30:duration=1',
         '-f', 'lavfi', '-i', 'sine=frequency=1000:sample_rate=44100:duration=1', '-c:v', 'libx264', '-profile:v', 'baseline',
         '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-ac', '2', '-shortest', '-movflags', '+faststart', clip])
    native = work / 'libmediaprobe.so'
    clang = args.ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    run([clang, '--target=aarch64-linux-android29', '-O2', '-Wall', '-Werror', '-fPIC', '-shared', tests / 'media_codec_probe.c',
         '-lmediandk', '-landroid', '-o', native])
    classes, dex = work / 'classes', work / 'dex'
    classes.mkdir(); dex.mkdir()
    run(['javac', '--release', '8', '-classpath', args.android_jar, '-d', classes, tests / 'MediaCodecProbe.java'])
    run(['java', '-cp', args.build_tools / 'lib/d8.jar', 'com.android.tools.r8.D8', '--min-api', '29', '--lib', args.android_jar,
         '--output', dex, *classes.rglob('*.class')])
    manifest = work / 'AndroidManifest.xml'
    manifest.write_text('<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.axrb.mediaprobe"><uses-sdk android:minSdkVersion="29" android:targetSdkVersion="32"/><application android:label="Media codec probe" android:extractNativeLibs="true"><activity android:name=".MediaCodecProbe" android:exported="true"><intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter></activity></application></manifest>')
    apk = work / 'media-probe.apk'
    run([args.build_tools / 'aapt2.exe', 'link', '-I', args.android_jar, '--manifest', manifest, '-o', apk])
    with zipfile.ZipFile(apk, 'a') as archive:
        archive.write(dex / 'classes.dex', 'classes.dex', zipfile.ZIP_DEFLATED)
        archive.write(native, 'lib/arm64-v8a/libmediaprobe.so', zipfile.ZIP_DEFLATED)
        # Uncompressed, so AAsset_openFileDescriptor can hand out the APK itself.
        archive.write(clip, 'assets/probe.mp4', zipfile.ZIP_STORED)
    direct = args.direct.resolve()
    command = prepare(apk=apk, activity='com.axrb.mediaprobe/com.axrb.mediaprobe.MediaCodecProbe', sysroot=args.sysroot, data=data,
                      bootstrap=direct / 'art/axrb-bootstrap.dex', native=direct / 'art/lib64', proxies=direct / 'guest/lib64')
    run([direct / 'axrb-direct.exe', *command], log=work / 'media.log', marker='MEDIA_CODEC_PROBE_PASS', timeout=180)


if __name__ == '__main__':
    main()
