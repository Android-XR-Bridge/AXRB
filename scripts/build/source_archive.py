"""Produce a source companion for the distributed launcher."""
from pathlib import Path
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[2]
version = json.loads((root / 'launcher/package.json').read_text())['version']
output = root / f'out/releases/AXRB-{version}-source.zip'
excluded = {'node_modules', 'out', 'dist', '__pycache__', '.git', '.local'}
files = []
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for directory in ('launcher', 'host', 'runtime', 'protocol', 'scripts', 'tests'):
        for file in (root / directory).rglob('*'):
            if file.is_file() and not excluded.intersection(file.relative_to(root).parts) and file.suffix not in ('.pyc', '.apk', '.so', '.keystore', '.jks', '.log'):
                files.append((file, file.relative_to(root).as_posix()))
    for name in ('CMakeLists.txt', 'CMakePresets.json', 'README.md', '.gitignore', 'logo_axrb.png'):
        files.append((root / name, name))
    for file, name in sorted(files, key=lambda entry: entry[1]):
        info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3
        info.external_attr = 0o644 << 16
        with file.open('rb') as source, archive.open(info, 'w') as target:
            shutil.copyfileobj(source, target)
print(output)
