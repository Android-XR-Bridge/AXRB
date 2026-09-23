"""Let the Android Emulator use the AVD's vCPU count on CPUs it distrusts.

Before starting Android, the emulator's QEMU backend asks CPUID leaf 1 whether
the host reports PCLMULQDQ, POPCNT and AES-NI. If any of the three is missing,
or hidden by a hypervisor or anti-cheat driver, it logs "Not all modern X86
virtualization features supported ... Setting AVD to run with 1 vCPU core
only" and overrides hw.cpu.ncore with 1. Nothing in the AVD or on the command
line can prevent this, and the launcher then sees 1 vCPU where it asked for
4 on every start.

This writes a copy of the backend beside the original, with the one branch
into that fallback replaced by NOPs, so the requested count (which the
emulator still caps at 6) is used. The original is never modified. The branch
is found by what it does rather than by a fixed offset: the only reference to
the fallback message, and the `call; test al, al; je` in front of it whose
jump lands on that fallback. Anything else is refused rather than guessed.

Machines that pass the check get nothing: the copy would behave exactly like
the original there, so it is not worth an unsigned 60 MB binary.

Run:  python qemu_multicore.py ensure --backend <sdk>\\emulator\\qemu\\windows-x86_64\\qemu-system-x86_64-headless.exe
"""
import argparse
import ctypes
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

MESSAGE = b'Not all modern X86 virtualization features supported'
# PCLMULQDQ (bit 1), POPCNT (bit 23) and AES-NI (bit 25) in CPUID.1:ECX, as
# tested by emulator 36.5.11 and 37.1.11.
REQUIRED_ECX = {1: 'PCLMULQDQ', 23: 'POPCNT', 25: 'AES-NI'}
REQUIRED_MASK = sum(1 << bit for bit in REQUIRED_ECX)
# How far in front of the message the guarding branch may sit. In the known
# builds it is 0x72 bytes: the capped-at-6 path lies between the two.
SEARCH_WINDOW = 0x100
MANIFEST_VERSION = 1


class PatchError(Exception):
    pass


def sections(image):
    if image[:2] != b'MZ':
        raise PatchError('not a Windows executable')
    header = struct.unpack_from('<I', image, 0x3c)[0]
    if image[header:header + 4] != b'PE\0\0':
        raise PatchError('not a Windows executable')
    count, optional = struct.unpack_from('<H12xH', image, header + 6)
    table = header + 24 + optional
    result = []
    for index in range(count):
        entry = table + index * 40
        name = image[entry:entry + 8].rstrip(b'\0')
        rva, raw_size, raw = struct.unpack_from('<4xIII', image, entry + 8)
        result.append((name, rva, raw, raw_size))
    return result


def rva_of(sections_, offset):
    for _, rva, raw, raw_size in sections_:
        if raw <= offset < raw + raw_size:
            return rva + offset - raw
    raise PatchError('offset outside every section')


def offset_of(sections_, rva):
    for _, start, raw, raw_size in sections_:
        if start <= rva < start + raw_size:
            return raw + rva - start
    raise PatchError('address outside the file')


# A RIP-relative lea into any register: REX.W or REX.WR, 8D, ModRM with mod=00
# and rm=101. The lookahead keeps overlapping candidates.
LEA = re.compile(rb'(?=[\x48\x4c]\x8d([\x05\x0d\x15\x1d\x25\x2d\x35\x3d])(.{4}))', re.S)
# call rel32; test al, al; je rel8 | je rel32
GUARD = re.compile(rb'(?=\xe8.{4}\x84\xc0(?:\x74(.)|\x0f\x84(.{4})))', re.S)


def locate(image):
    """Return the fallback branch as (file offset, length, rva, detail)."""
    table = sections(image)
    text = next((s for s in table if s[0] == b'.text'), None)
    if text is None:
        raise PatchError('no code section')
    hits = [m.start() for m in re.finditer(re.escape(MESSAGE), image)]
    if len(hits) != 1:
        raise PatchError(f'the single-vCPU message appears {len(hits)} times, expected once')
    message = rva_of(table, hits[0])
    _, text_rva, text_raw, text_size = text
    code = image[text_raw:text_raw + text_size]
    references = []
    for match in LEA.finditer(code):
        end = text_rva + match.start() + 7
        if end + struct.unpack('<i', match.group(2))[0] == message:
            references.append(end - 7)
    if len(references) != 1:
        raise PatchError(f'the single-vCPU message is referenced {len(references)} times, expected once')
    reference = references[0]
    start = max(text_rva, reference - SEARCH_WINDOW)
    window = code[start - text_rva:reference - text_rva]
    guards = []
    for match in GUARD.finditer(window):
        branch = start + match.start() + 7
        if match.group(1) is not None:
            length, displacement = 2, struct.unpack('<b', match.group(1))[0]
        else:
            length, displacement = 6, struct.unpack('<i', match.group(2))[0]
        target = branch + length + displacement
        # The jump has to land on the fallback: after itself, no later than
        # the message it prints.
        if branch + length < target <= reference:
            guards.append((branch, length, start + match.start()))
    if len(guards) != 1:
        raise PatchError(f'found {len(guards)} candidate checks in front of the single-vCPU fallback, expected one')
    branch, length, call = guards[0]
    return offset_of(table, branch), length, branch, describe_check(image, table, call)


def describe_check(image, table, call):
    """Name the predicate when it is the known CPUID test; informational only."""
    try:
        target = call + 5 + struct.unpack_from('<i', image, offset_of(table, call) + 1)[0]
        for _ in range(2):  # Incremental-link thunks: jmp rel32.
            offset = offset_of(table, target)
            if image[offset] != 0xe9:
                break
            target = target + 5 + struct.unpack_from('<i', image, offset + 1)[0]
        body = image[offset_of(table, target):offset_of(table, target) + 0x40]
        # not eax; test eax, 0x2800002: every required bit must be set.
        if struct.pack('<BI', 0xa9, REQUIRED_MASK) in body:
            return 'CPUID.1:ECX & 0x2800002'
    except (PatchError, IndexError, struct.error):
        pass
    return 'unrecognised predicate'


def patch(image):
    offset, length, rva, check = locate(image)
    patched = bytearray(image)
    patched[offset:offset + length] = b'\x90' * length
    return bytes(patched), {'offset': offset, 'rva': rva, 'length': length, 'check': check}


def host_cpuid_ecx():
    """CPUID.1:ECX as this process sees it, which is what QEMU will see."""
    if sys.platform != 'win32' or struct.calcsize('P') != 8:
        return None
    # push rbx; mov r8,rcx; mov eax,1; xor ecx,ecx; cpuid; mov [r8],ecx; pop rbx; ret
    stub = bytes.fromhex('53' '4989c8' 'b801000000' '31c9' '0fa2' '418908' '5b' 'c3')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.VirtualAlloc.restype = ctypes.c_void_p
    kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
    kernel.VirtualProtect.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint32)]
    kernel.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]
    memory = kernel.VirtualAlloc(None, len(stub), 0x3000, 0x04)  # MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE
    if not memory:
        return None
    try:
        ctypes.memmove(memory, stub, len(stub))
        old = ctypes.c_uint32()
        if not kernel.VirtualProtect(memory, len(stub), 0x20, ctypes.byref(old)):  # PAGE_EXECUTE_READ
            return None
        value = ctypes.c_uint32()
        ctypes.CFUNCTYPE(None, ctypes.c_void_p)(memory)(ctypes.addressof(value))
        return value.value
    finally:
        kernel.VirtualFree(memory, 0, 0x8000)  # MEM_RELEASE


def missing_features(ecx):
    return [name for bit, name in REQUIRED_ECX.items() if not ecx >> bit & 1]


def target_for(backend):
    return backend.with_name(backend.stem + '-multicore' + backend.suffix)


def fingerprint(path):
    stat = path.stat()
    return {'size': stat.st_size, 'mtime_ns': stat.st_mtime_ns}


def runs(executable):
    """A copy that Smart App Control or antivirus blocks must not replace a working emulator."""
    emulator = executable.parents[2]
    environment = dict(os.environ)
    environment['PATH'] = os.pathsep.join([str(emulator), str(emulator / 'lib64'), environment.get('PATH', '')])
    environment['ANDROID_EMULATOR_LAUNCHER_DIR'] = str(emulator)
    try:
        result = subprocess.run([str(executable), '-version'], capture_output=True, timeout=60,
                                env=environment, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except (OSError, subprocess.TimeoutExpired) as error:
        return f'{type(error).__name__}: {error}'
    if result.returncode != 0 or b'Android emulator version' not in result.stdout:
        return f'exit {result.returncode}: ' + (result.stderr or result.stdout).decode(errors='replace').strip()[:200]
    return None


def ensure(backend, force=False, ecx=None):
    backend = Path(backend)
    if ecx is None:
        ecx = host_cpuid_ecx()
    report = {'backend': str(backend)}
    if ecx is not None:
        report['cpuid_ecx'] = f'{ecx:#010x}'
        missing = missing_features(ecx)
        report['missing'] = missing
        if not missing and not force:
            return {**report, 'status': 'not-needed'}
    if not backend.is_file():
        return {**report, 'status': 'unavailable', 'reason': f'{backend.name} is missing'}
    target = target_for(backend)
    manifest = target.with_suffix('.json')
    source = fingerprint(backend)
    try:
        previous = json.loads(manifest.read_text())
        if previous.get('version') == MANIFEST_VERSION and previous.get('source') == source and target.is_file() \
                and previous.get('target') == fingerprint(target):
            return {**report, **previous['patch'], 'status': 'ready', 'target': str(target)}
    except (OSError, ValueError):
        pass
    image = backend.read_bytes()
    try:
        patched, details = patch(image)
    except PatchError as error:
        return {**report, 'status': 'unsupported', 'reason': str(error)}
    details['sha256'] = hashlib.sha256(patched).hexdigest()
    staging = target.with_name(target.name + '.partial')
    manifest.unlink(missing_ok=True)
    staging.write_bytes(patched)
    os.replace(staging, target)
    failure = runs(target)
    if failure:
        target.unlink(missing_ok=True)
        return {**report, 'status': 'blocked', 'reason': f'the patched copy did not run ({failure})'}
    manifest.write_text(json.dumps({'version': MANIFEST_VERSION, 'source': source,
                                    'target': fingerprint(target), 'patch': details}))
    return {**report, **details, 'status': 'patched', 'target': str(target)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('probe', help='report whether this CPU trips the single-vCPU fallback')
    make = commands.add_parser('ensure', help='create or reuse the multi-core copy when this CPU needs it')
    make.add_argument('--backend', required=True, type=Path)
    make.add_argument('--force', action='store_true', help='patch even when this CPU passes the check')
    arguments = parser.parse_args()
    if arguments.command == 'probe':
        ecx = host_cpuid_ecx()
        if ecx is None:
            print(json.dumps({'status': 'unknown'}))
        else:
            missing = missing_features(ecx)
            print(json.dumps({'cpuid_ecx': f'{ecx:#010x}', 'missing': missing,
                              'status': 'affected' if missing else 'not-affected'}))
        return
    # For support and testing on a CPU that passes: the copy then behaves
    # exactly like the original, so forcing it proves the launch path only.
    force = arguments.force or os.environ.get('AXRB_QEMU_MULTICORE') == 'force'
    try:
        result = ensure(arguments.backend, force=force)
    except OSError as error:
        # An SDK in a read-only folder, or a copy locked by a running emulator.
        result = {'backend': str(arguments.backend), 'status': 'unwritable', 'reason': str(error)}
    print(json.dumps(result))


if __name__ == '__main__':
    main()
