"""Start the isolated test emulator the same way session.py does."""
import sys
from session import ROOT, boot_emulator
boot_emulator(ROOT / 'out/cpu-work/boot-manual.log', int(sys.argv[1]) if len(sys.argv) > 1 else 4)
print('ready')
