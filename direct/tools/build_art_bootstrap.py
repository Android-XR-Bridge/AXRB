"""Compile the Direct AOSP bootstrap using developer Android/JDK tools."""
import argparse
from pathlib import Path
import subprocess
import shutil
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--android-jar', type=Path, required=True)
    p.add_argument('--d8', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    javac, java = shutil.which('javac'), shutil.which('java')
    if not javac or not java:
        p.error('Developer JDK must be on PATH')
    source = Path(__file__).resolve().parents[1] / 'art/java'
    dex = a.output / 'dex'
    dex.mkdir(parents=True, exist_ok=True)
    # A failed compile must not publish a stale or partially rebuilt bootstrap.
    with tempfile.TemporaryDirectory(prefix='bootstrap-', dir=a.output) as staging:
        classes, built = Path(staging) / 'classes', Path(staging) / 'dex'
        classes.mkdir()
        built.mkdir()
        subprocess.run([javac, '--release', '8', '-Xlint:-options', '-cp', str(a.android_jar),
                        '-d', str(classes), *map(str, sorted(source.rglob('*.java')))], check=True)
        subprocess.run([java, '-cp', str(a.d8), 'com.android.tools.r8.D8', '--min-api', '26',
                        '--lib', str(a.android_jar), '--output', str(built),
                        *map(str, sorted(classes.rglob('*.class')))], check=True)
        (built / 'classes.dex').replace(dex / 'classes.dex')
    print(dex / 'classes.dex')


if __name__ == '__main__':
    main()
