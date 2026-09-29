"""Stream data from the guest to the host through `adb reverse` (the frame path) and report stalls.

The guest writes 64 KiB blocks to a host TCP server over an adb reverse tunnel
while the host acknowledges each block, like the image transport. Prints the
throughput per second and every gap longer than 100 ms.
"""
import argparse
import socket
import subprocess
import threading
import time

from session import adb

PORT = 38555


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=int, default=60)
    a = p.parse_args()
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(('127.0.0.1', PORT))
    server.listen(1)
    adb('reverse', f'tcp:{PORT}', f'tcp:{PORT}')
    guest = subprocess.Popen(['python', '-c', f'''
import subprocess
from session import ADB, SERIAL
subprocess.run([ADB, "-P", "5038", "-s", SERIAL, "shell",
    "i=0; while [ $i -lt 1000000 ]; do head -c 65536 /dev/zero; i=$((i+1)); done | toybox nc 127.0.0.1 {PORT} > /dev/null"])
'''], cwd=str(__import__('pathlib').Path(__file__).parent))
    connection, _ = server.accept()
    connection.settimeout(10)
    total, second_bytes, last = 0, 0, time.monotonic()
    start = second_start = last
    stalls = []
    try:
        while time.monotonic() - start < a.seconds:
            try:
                data = connection.recv(1 << 20)
            except socket.timeout:
                print('STALL: no data for 10 s', flush=True)
                break
            if not data:
                print('connection closed', flush=True)
                break
            now = time.monotonic()
            if now - last > 0.1:
                stalls.append(now - last)
                print(f'gap {1000 * (now - last):.0f} ms at {now - start:.1f} s', flush=True)
            last = now
            total += len(data)
            second_bytes += len(data)
            if now - second_start >= 1:
                print(f'{now - start:5.1f} s {second_bytes / (now - second_start) / 1e6:7.1f} MB/s', flush=True)
                second_bytes, second_start = 0, now
    finally:
        connection.close()
        guest.kill()
        adb('reverse', '--remove', f'tcp:{PORT}', check=False)
    print(f'total {total / 1e6:.0f} MB, {len(stalls)} gaps > 100 ms, max {max(stalls, default=0) * 1000:.0f} ms')


if __name__ == '__main__':
    main()
