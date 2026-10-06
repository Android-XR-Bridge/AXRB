"""Export a normalized, reproducible patch from the pinned Digitalis checkout."""
import argparse
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
args = parser.parse_args()
data = subprocess.run(['git', '-C', str(args.source), 'diff', '--binary', 'HEAD'],
                      check=True, capture_output=True).stdout
Path(__file__).with_name('digitalis-axrb-performance.patch').write_bytes(data)
