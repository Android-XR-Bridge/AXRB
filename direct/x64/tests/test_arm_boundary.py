from pathlib import Path
import shutil
import subprocess
import sys
runner, root, binary, fixture = map(Path, sys.argv[1:])
target=root/'data/local/tmp/arm-boundary'
target.mkdir(parents=True,exist_ok=True)
shutil.copyfile(fixture,target/'input.so')
result=subprocess.run([sys.executable,str(Path(__file__).with_name('test_integration.py')),
    str(runner),str(root),str(binary),'NATIVE_X64_ARM_BOUNDARY_PASS'])
raise SystemExit(result.returncode)
