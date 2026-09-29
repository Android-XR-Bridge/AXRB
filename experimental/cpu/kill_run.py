"""Stop a stuck benchmark: harness processes and the isolated test emulator only."""
import os
import psutil
me = os.getpid()
for p in psutil.process_iter(['name', 'cmdline']):
    if p.pid == me:
        continue
    cmd = p.info['cmdline'] or []
    name = p.info['name'] or ''
    harness = name.startswith('python') and any(part.endswith('session.py') for part in cmd)
    emulator = name.startswith('qemu-system') and 'axrb-digitalis-test' in cmd
    launcher = name.lower().startswith('powershell') and any(part.endswith('run_windows_game.ps1') for part in cmd)
    if harness or emulator or launcher or name == 'axrb-host-bridge.exe':
        print('kill', p.pid, name)
        p.kill()
