import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/emulator'))
from translator import verify_bundle


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.bundle = Path(self.temp.name)
        self.files = {
            'system/lib64/libberberis_arm64.so': b'\x7fELF\x02\x01' + bytes(12) + (62).to_bytes(2, 'little'),
            'system/lib64/arm64/libc.so': b'\x7fELF\x02\x01' + bytes(12) + (183).to_bytes(2, 'little'),
            'system/bin/arm64/linker64': b'guest linker',
            'system/etc/ld.config.arm64.txt': b'guest namespaces',
        }
        lines = []
        for name, data in self.files.items():
            file = self.bundle / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
            lines.append(hashlib.sha256(data).hexdigest() + '  ' + name)
        (self.bundle / 'SHA256SUMS').write_text('\n'.join(lines))

    def test_verifies_complete_payload(self):
        self.assertEqual(verify_bundle(self.bundle), hashlib.sha256(self.files['system/lib64/libberberis_arm64.so']).hexdigest())

    def test_rejects_modified_payload(self):
        (self.bundle / 'system/lib64/arm64/libc.so').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum'): verify_bundle(self.bundle)

    def test_rejects_unchecked_system_files(self):
        (self.bundle / 'system/lib64/extra.so').write_bytes(b'unknown')
        with self.assertRaisesRegex(ValueError, 'unchecked'): verify_bundle(self.bundle)

    def test_rejects_manifest_traversal(self):
        (self.bundle / 'SHA256SUMS').write_text('0' * 64 + '  ../outside')
        with self.assertRaisesRegex(ValueError, 'Invalid'): verify_bundle(self.bundle)


if __name__ == '__main__': unittest.main()
