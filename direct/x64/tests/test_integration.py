"""Run an actual Android binary and require its success marker plus clean exit."""
from pathlib import Path
import subprocess
import sys
runner, root, binary, marker = sys.argv[1:]
inspection = subprocess.run([runner, '--inspect', binary], capture_output=True, text=True, timeout=15)
if inspection.returncode:
    raise SystemExit(inspection.stderr)
if 'FSGSBASE=no' in inspection.stdout:
    raise SystemExit(77)
result = subprocess.run([runner, '--run-linker', root, binary], capture_output=True, text=True, timeout=20)
print(result.stdout)
if result.returncode or marker not in result.stdout:
    print(result.stderr)
    raise SystemExit(f'Failed Android integration: exit={result.returncode}, expected={marker}')
