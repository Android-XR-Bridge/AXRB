"""Produce a source companion for the distributed launcher."""
from pathlib import Path
import json
import zipfile

root = Path(__file__).resolve().parents[2]
version = json.loads((root / 'launcher/package.json').read_text())['version']
output = root / f'out/releases/AXRB-{version}-source.zip'
excluded = {'node_modules', 'out', 'dist', '__pycache__', '.git', '.local'}
with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
    for directory in ('launcher', 'host', 'runtime', 'protocol', 'scripts', 'tests'):
        for file in (root / directory).rglob('*'):
            if file.is_file() and not excluded.intersection(file.relative_to(root).parts) and file.suffix not in ('.pyc', '.apk', '.so', '.keystore', '.jks', '.log'):
                archive.write(file, file.relative_to(root))
    for name in ('CMakeLists.txt', 'CMakePresets.json', 'README.md', '.gitignore', 'logo_axrb.png'):
        archive.write(root / name, name)
print(output)
