"""Pushes out/tmp/lat/pingpong and runs it for CPU pairs on the booted test AVD."""
import subprocess, os
from session import adb, ROOT
env = dict(os.environ, MSYS_NO_PATHCONV='1')
adb('push', str(ROOT / 'out/tmp/lat/pingpong'), '/data/local/tmp/pingpong')
adb('shell', 'chmod 755 /data/local/tmp/pingpong')
for other in (1, 2, 3):
    for _ in range(2):
        r = adb('shell', f'/data/local/tmp/pingpong 200000 {other}', check=False)
        print(r.stdout.strip() if hasattr(r, 'stdout') else r, flush=True)
