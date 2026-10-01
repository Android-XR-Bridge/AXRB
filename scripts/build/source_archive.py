"""Produce the shared source companion and refresh available package checksums."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[2]
version = json.loads((root / 'launcher/package.json').read_text())['version']
output = root / f'out/releases/AXRB-{version}-source.zip'
excluded = {'node_modules', 'out', 'dist', '__pycache__', '.git', '.local', 'meta-session.txt', 'meta-session.bin'}
files = []
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for directory in ('launcher', 'host', 'runtime', 'protocol', 'scripts', 'tests', 'modules', 'licenses', '.github'):
        for file in (root / directory).rglob('*'):
            if file.is_file() and not excluded.intersection(file.relative_to(root).parts) and file.suffix not in ('.pyc', '.apk', '.so', '.keystore', '.jks', '.log'):
                files.append((file, file.relative_to(root).as_posix()))
    for name in ('CMakeLists.txt', 'CMakePresets.json', 'README.md', '.gitignore', 'logo_axrb.png', 'LICENSE', 'CONTRIBUTING.md'):
        files.append((root / name, name))
    for file, name in sorted(files, key=lambda entry: entry[1]):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3
        info.external_attr = 0o644 << 16
        with file.open('rb') as source, archive.open(info, 'w') as target:
            shutil.copyfileobj(source, target)
print(output)


def sha256(file):
    digest = hashlib.sha256()
    with file.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


# Both modes share this source ZIP. Refresh every available package's manifest
# only after the archive is closed, including the mode not rebuilt this time.
source_entry = f'{sha256(output)}  {output.name}\n'
for name, suffix in ((f'AXRB-{version}.exe', ''), (f'AXRB-Portable-{version}.zip', '-portable'), (f'AXRB-Setup-{version}.exe', '-setup')):
    package = output.parent / name
    manifest = output.parent / f'SHA256SUMS-{version}{suffix}.txt'
    if package.is_file():
        manifest.write_text(f'{sha256(package)}  {name}\n{source_entry}', encoding='ascii')
    else:
        manifest.unlink(missing_ok=True)
