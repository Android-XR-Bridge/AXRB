import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('qemu_multicore',
    Path(__file__).resolve().parents[2] / 'scripts/emulator/qemu_multicore.py')
multicore = importlib.util.module_from_spec(spec)
spec.loader.exec_module(multicore)

TEXT_RVA, TEXT_RAW, RDATA_RVA, RDATA_RAW, SIZE = 0x1000, 0x400, 0x3000, 0x2400, 0x2000
MESSAGE = multicore.MESSAGE + b', which introduces problems. Setting AVD to run with 1 vCPU core only.\0'
PREDICATE = 0x1800  # CPUID helper body inside .text


def image(code_at=0x100, guard=True, branch=b'\x74', references=1, predicate=True):
    """A two-section PE laid out like the emulator's check.

    call predicate; test al, al; je fallback; <capped path>; fallback: lea rax, [message]
    """
    data = bytearray(RDATA_RAW + SIZE)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H12xH', data, 0x86, 2, 0xf0)
    table = 0x80 + 24 + 0xf0
    for index, (name, rva, raw) in enumerate(((b'.text', TEXT_RVA, TEXT_RAW), (b'.rdata', RDATA_RVA, RDATA_RAW))):
        entry = table + index * 40
        data[entry:entry + 8] = name.ljust(8, b'\0')
        struct.pack_into('<IIII', data, entry + 8, SIZE, rva, SIZE, raw)
    data[RDATA_RAW + 0x10:RDATA_RAW + 0x10 + len(MESSAGE)] = MESSAGE
    message = RDATA_RVA + 0x10

    def emit(rva, code):
        data[TEXT_RAW + rva - TEXT_RVA:TEXT_RAW + rva - TEXT_RVA + len(code)] = code

    call = TEXT_RVA + code_at
    fallback = call + 9 + 0x40 if branch == b'\x74' else call + 13 + 0x40
    body = struct.pack('<Bi', 0xe8, PREDICATE + TEXT_RVA - (call + 5)) + b'\x84\xc0'
    if not guard:
        body = b'\x90' * 7
    if branch == b'\x74':
        body += b'\x74' + struct.pack('<b', fallback - (call + 9))
    else:
        body += b'\x0f\x84' + struct.pack('<i', fallback - (call + 13))
    emit(call, body)
    for index in range(references):
        lea = fallback + index * 0x20
        emit(lea, b'\x48\x8d\x05' + struct.pack('<i', message - (lea + 7)))
    helper = b'\x0f\xa2\xf7\xd0' + (struct.pack('<BI', 0xa9, multicore.REQUIRED_MASK) if predicate else b'\xa9\0\0\0\0')
    emit(TEXT_RVA + PREDICATE, helper)
    return bytes(data), TEXT_RAW + call - TEXT_RVA + 7


class LocateTest(unittest.TestCase):
    def test_finds_short_branch_into_fallback(self):
        data, branch = image()
        offset, length, rva, check = multicore.locate(data)
        self.assertEqual((offset, length), (branch, 2))
        self.assertEqual(rva, branch - TEXT_RAW + TEXT_RVA)
        self.assertEqual(check, 'CPUID.1:ECX & 0x2800002')

    def test_finds_near_branch_into_fallback(self):
        data, branch = image(branch=b'\x0f\x84')
        self.assertEqual(multicore.locate(data)[:2], (branch, 6))

    def test_patch_replaces_only_the_branch(self):
        data, branch = image()
        patched, details = multicore.patch(data)
        self.assertEqual(patched[branch:branch + 2], b'\x90\x90')
        self.assertEqual([i for i in range(len(data)) if data[i] != patched[i]], [branch, branch + 1])
        self.assertEqual(details['length'], 2)

    def test_patched_image_is_refused_rather_than_patched_again(self):
        patched, _ = multicore.patch(image()[0])
        with self.assertRaisesRegex(multicore.PatchError, 'found 0 candidate'):
            multicore.locate(patched)

    def test_refuses_without_a_guarding_check(self):
        with self.assertRaisesRegex(multicore.PatchError, 'found 0 candidate'):
            multicore.locate(image(guard=False)[0])

    def test_refuses_ambiguous_message_references(self):
        with self.assertRaisesRegex(multicore.PatchError, 'referenced 2 times'):
            multicore.locate(image(references=2)[0])

    def test_refuses_an_unknown_build(self):
        data = bytearray(image()[0])
        at = data.index(multicore.MESSAGE)
        data[at:at + 3] = b'Now'
        with self.assertRaisesRegex(multicore.PatchError, 'appears 0 times'):
            multicore.locate(bytes(data))

    def test_refuses_non_executables(self):
        with self.assertRaisesRegex(multicore.PatchError, 'not a Windows executable'):
            multicore.locate(b'\0' * 0x400)

    def test_unknown_predicate_is_reported_not_refused(self):
        self.assertEqual(multicore.locate(image(predicate=False)[0])[3], 'unrecognised predicate')


class EnsureTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        root = Path(self.directory.name) / 'emulator/qemu/windows-x86_64'
        root.mkdir(parents=True)
        self.backend = root / 'qemu-system-x86_64-headless.exe'
        self.data, self.branch = image()
        self.backend.write_bytes(self.data)
        self.runs = patch.object(multicore, 'runs', return_value=None)
        self.runs.start()

    def tearDown(self):
        patch.stopall()
        self.directory.cleanup()

    def test_cpu_that_passes_gets_no_copy(self):
        result = multicore.ensure(self.backend, ecx=multicore.REQUIRED_MASK)
        self.assertEqual(result['status'], 'not-needed')
        self.assertFalse(multicore.target_for(self.backend).exists())

    def test_names_every_hidden_feature(self):
        self.assertEqual(multicore.missing_features(0), ['PCLMULQDQ', 'POPCNT', 'AES-NI'])
        self.assertEqual(multicore.missing_features(multicore.REQUIRED_MASK & ~(1 << 25)), ['AES-NI'])

    def test_writes_copy_beside_original_and_keeps_original(self):
        result = multicore.ensure(self.backend, ecx=0)
        target = Path(result['target'])
        self.assertEqual((result['status'], result['missing']), ('patched', ['PCLMULQDQ', 'POPCNT', 'AES-NI']))
        self.assertEqual(target.name, 'qemu-system-x86_64-headless-multicore.exe')
        self.assertEqual(target.parent, self.backend.parent)
        self.assertEqual(self.backend.read_bytes(), self.data)
        self.assertEqual(target.read_bytes()[self.branch:self.branch + 2], b'\x90\x90')

    def test_reuses_copy_until_the_original_changes(self):
        multicore.ensure(self.backend, ecx=0)
        self.assertEqual(multicore.ensure(self.backend, ecx=0)['status'], 'ready')
        self.backend.write_bytes(self.data + b'\0')
        self.assertEqual(multicore.ensure(self.backend, ecx=0)['status'], 'patched')

    def test_rebuilds_a_copy_that_was_changed(self):
        target = Path(multicore.ensure(self.backend, ecx=0)['target'])
        target.write_bytes(b'quarantined')
        self.assertEqual(multicore.ensure(self.backend, ecx=0)['status'], 'patched')

    def test_removes_a_copy_that_cannot_run(self):
        with patch.object(multicore, 'runs', return_value='exit 5: blocked'):
            result = multicore.ensure(self.backend, ecx=0)
        self.assertEqual(result['status'], 'blocked')
        self.assertIn('exit 5', result['reason'])
        self.assertFalse(multicore.target_for(self.backend).exists())

    def test_reports_an_unpatchable_build(self):
        self.backend.write_bytes(image(guard=False)[0])
        result = multicore.ensure(self.backend, ecx=0)
        self.assertEqual(result['status'], 'unsupported')
        self.assertFalse(multicore.target_for(self.backend).exists())

    def test_force_patches_on_a_cpu_that_passes(self):
        result = multicore.ensure(self.backend, force=True, ecx=multicore.REQUIRED_MASK)
        self.assertEqual(result['status'], 'patched')

    def test_missing_backend(self):
        self.backend.unlink()
        self.assertEqual(multicore.ensure(self.backend, ecx=0)['status'], 'unavailable')


@unittest.skipUnless(os.name == 'nt', 'CPUID probe runs on Windows only')
class ProbeTest(unittest.TestCase):
    def test_reads_leaf_one(self):
        ecx = multicore.host_cpuid_ecx()
        self.assertIsInstance(ecx, int)
        # SSE3 (bit 0) is on every x86-64 CPU that can run Windows 11.
        self.assertTrue(ecx & 1)


class InstalledEmulatorTest(unittest.TestCase):
    """Emulator 36.5.11, which AXRB installs, when it is present here."""

    def test_locates_the_check_in_the_managed_backend(self):
        sdk = Path(os.environ.get('LOCALAPPDATA', '')) / 'AXRB Runtime/sdk/emulator/qemu/windows-x86_64'
        found = [name for name in ('qemu-system-x86_64-headless.exe', 'qemu-system-x86_64.exe') if (sdk / name).is_file()]
        if not found:
            self.skipTest('no managed emulator installed')
        for name in found:
            data = (sdk / name).read_bytes()
            offset, length, _, check = multicore.locate(data)
            self.assertEqual(data[offset - 7], 0xe8)
            self.assertEqual(data[offset - 2:offset], b'\x84\xc0')
            self.assertEqual((data[offset], length), (0x74, 2))
            self.assertEqual(check, 'CPUID.1:ECX & 0x2800002')


if __name__ == '__main__':
    unittest.main()
