"""Prefer the launcher's standalone tools; an SDK is only a source-tree fallback."""
from pathlib import Path
import re


def find_aapt2(sdk=None, root=None):
    root = Path(root) if root is not None else Path(__file__).resolve().parents[2]
    bundled = root / 'tools/aapt2/aapt2.exe'
    if bundled.is_file() and bundled.stat().st_size:
        return bundled
    # A packaged installation must not depend on a developer's SDK by accident.
    if (root / 'distribution.json').is_file():
        raise RuntimeError('The launcher APK reader is missing. Reinstall the AXRB launcher.')
    versions = list((Path(sdk) / 'build-tools').glob('*/aapt2.exe')) if sdk else []
    if versions:
        return max(versions, key=lambda p: tuple(int(x) for x in re.findall(r'\d+', p.parent.name)))
    raise RuntimeError('APK reader not found. Build the launcher to bundle aapt2, or configure an SDK for source-tree development.')
