import importlib.util
from pathlib import Path
import unittest
import argparse
import base64
import json
import tempfile
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('policy', Path(__file__).resolve().parents[2] / 'scripts/emulator/unreal_memory_policy.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class PolicyTests(unittest.TestCase):
    def test_budget_leaves_headroom(self):
        for capacity in (2048, 3072, 4096, 8192, 16303, 24564):
            pool = policy.pool_mib(capacity)
            self.assertLessEqual(pool, capacity // 2)
            self.assertGreaterEqual(capacity - pool, 2048)
            self.assertEqual(pool % 256, 0)
        self.assertEqual(policy.pool_mib(16303), 7936)

    def test_preserves_user_settings_and_overrides_old_force_load(self):
        original = '[SystemSettings]\nr.Streaming.FullyLoadUsedTextures=1\ncustom=7\n[Other]\nx=2\n'
        changed = policy.configure(original, 7936, 2048)
        self.assertTrue(changed.startswith(original))
        self.assertIn('r.Streaming.FullyLoadUsedTextures=0', changed)
        self.assertIn('r.Streaming.PoolSize=7936', changed)
        self.assertEqual(changed.count(policy.BEGIN), 1)
        self.assertIn('PoolSizeVRAMPercentage=387', changed)

    def test_unknown_managed_block_is_not_overwritten(self):
        with self.assertRaises(ValueError):
            policy.configure(policy.BEGIN + '\nuser edit', 4096, 2048)

    def test_percentage_rounds_down_and_handles_uncapped_heaps(self):
        for host, guest in ((16303, 2048), (8192, 2048), (16303, 16384), (24564, 24576)):
            target = policy.pool_mib(host)
            percentage = policy.pool_percentage(target, guest)
            effective = guest * percentage // 100
            self.assertLessEqual(effective, target)
            self.assertLess(target - effective, guest / 100 + 1)
        with self.assertRaises(ValueError):
            policy.pool_percentage(4096, 0)


class AdapterTests(unittest.TestCase):
    def fixtures(self, vendor=0x1002, capacity=8192):
        guest = {'properties': {'vendorID': vendor, 'deviceID': 123, 'deviceType': 2},
                 'memory': {'memoryHeaps': [{'size': '0x80000000', 'flags': 1}]}}
        host = {'vendor_id': vendor, 'device_id': 123, 'flags': 0,
                'dedicated_video_bytes': capacity * 1024**2, 'shared_system_bytes': 32 * 1024**3}
        return guest, host

    def test_amd_and_nvidia_match_correct_adapter(self):
        for vendor in (0x1002, 0x10de):
            guest, host = self.fixtures(vendor)
            other = {**host, 'vendor_id': 0x10de if vendor == 0x1002 else 0x1002,
                     'dedicated_video_bytes': 24 * 1024**3}
            self.assertEqual(policy.memory_budget([guest], [other, host]), (8192, 4096, 2048))

    def test_shared_ram_does_not_inflate_integrated_vram(self):
        guest, host = self.fixtures(capacity=512)
        guest['properties']['deviceType'] = 1
        with self.assertRaisesRegex(ValueError, 'dedicated VRAM'):
            policy.memory_budget([guest], [host])

    def test_ambiguous_software_and_mismatched_adapters(self):
        guest, host = self.fixtures()
        # Duplicate entries for one PCI id are covered in AdapterMatchingTests:
        # identical VRAM is accepted, a disagreement is still ambiguous.
        for adapters in ([], [{**host, 'flags': 2}], [{**host, 'device_id': 999}],
                         [host, {**host, 'dedicated_video_bytes': 4096 * 1024**2}]):
            with self.assertRaises(ValueError):
                policy.memory_budget([guest], adapters)
        guest['properties']['deviceType'] = 4
        with self.assertRaises(ValueError):
            policy.memory_budget([guest], [host])

    def test_failed_query_restores_previous_budget_without_losing_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            args = argparse.Namespace(package='com.example.game', serial='emulator-5584',
                                      sdk=Path('sdk'), state_dir=Path(directory), restore=False)
            original = '[Audio]\nVolume=0.5\n'
            current = policy.configure(original, 7936, 2048)
            state_path = Path(directory) / 'emulator-5584-com.example.game.json'
            state_path.write_text(json.dumps({'path': '/config/Engine.ini', 'original': original,
                                             'applied_sha256': policy.digest(current)}))
            writes = []
            def command(argv, **kwargs):
                cmd = str(argv[-1])
                if 'dumpsys package' in cmd: return 'nativeLibraryDir=/lib'
                if 'test -d' in cmd: return 'yes'
                if 'find /lib' in cmd: return '/lib/libUnreal.so'
                if 'cmd gpu vkjson' in cmd: return '{"devices": []}'
                if 'base64 /config' in cmd: return base64.b64encode((original if writes else current).encode()).decode()
                if 'pidof' in cmd: return ''
                if 'stat -c' in cmd: return '10001'
                if 'umask 077' in cmd: writes.append(cmd); return ''
                raise AssertionError(cmd)
            with patch.object(policy, 'run', side_effect=command), patch.object(policy, 'host_adapters', side_effect=OSError('DXGI unavailable')):
                self.assertEqual(policy.apply(args)['status'], 'restored')
            self.assertFalse(state_path.exists())
            self.assertEqual(len(writes), 1)


if __name__ == '__main__':
    unittest.main()


class AdapterMatchingTests(unittest.TestCase):
    GUEST = [{'properties': {'vendorID': 0x1002, 'deviceID': 0x73ff, 'deviceType': 2,
                             'deviceName': 'AMD Radeon RX 6600'},
              'memory': {'memoryHeaps': [{'size': 8 * 1024 ** 3, 'flags': 1}]}}]

    @staticmethod
    def adapter(name, vendor, device, mib, flags=0):
        return {'name': name, 'vendor_id': vendor, 'device_id': device, 'flags': flags,
                'dedicated_video_bytes': mib * 1024 * 1024}

    def test_discrete_gpu_is_matched_next_to_integrated_graphics(self):
        adapters = [self.adapter('AMD Radeon RX 6600', 0x1002, 0x73ff, 8192),
                    self.adapter('AMD Radeon(TM) Graphics', 0x1002, 0x13c0, 485),
                    self.adapter('Microsoft Basic Render Driver', 0x1414, 0x008c, 0, flags=2)]
        capacity, pool, guest_heap = policy.memory_budget(self.GUEST, adapters)
        self.assertEqual(capacity, 8192)
        self.assertEqual(pool, policy.pool_mib(8192))
        self.assertEqual(guest_heap, 8192)

    def test_duplicate_entries_for_one_model_are_not_treated_as_ambiguous(self):
        # The same PCI id twice is the same hardware, so the budget is unchanged.
        adapters = [self.adapter('AMD Radeon RX 6600', 0x1002, 0x73ff, 8192),
                    self.adapter('AMD Radeon RX 6600', 0x1002, 0x73ff, 8192)]
        capacity, _, _ = policy.memory_budget(self.GUEST, adapters)
        self.assertEqual(capacity, 8192)

    def test_conflicting_vram_for_one_id_still_skips(self):
        adapters = [self.adapter('AMD Radeon RX 6600', 0x1002, 0x73ff, 8192),
                    self.adapter('AMD Radeon RX 6600', 0x1002, 0x73ff, 4096)]
        with self.assertRaises(ValueError):
            policy.memory_budget(self.GUEST, adapters)

    def test_no_match_names_the_guest_gpu_and_every_candidate(self):
        adapters = [self.adapter('AMD Radeon(TM) Graphics', 0x1002, 0x13c0, 485),
                    self.adapter('Microsoft Basic Render Driver', 0x1414, 0x008c, 0, flags=2)]
        with self.assertRaises(ValueError) as caught:
            policy.memory_budget(self.GUEST, adapters)
        message = str(caught.exception)
        self.assertIn('AMD Radeon RX 6600', message)
        self.assertIn('0x73ff', message, 'the unmatched guest id must be reported')
        self.assertIn('0x13c0', message, 'the adapters that were considered must be listed')
        self.assertNotIn('Basic Render Driver', message, 'software adapters are not candidates')

    def test_software_adapters_never_satisfy_the_match(self):
        adapters = [self.adapter('Soft RX 6600', 0x1002, 0x73ff, 8192, flags=2)]
        with self.assertRaises(ValueError):
            policy.memory_budget(self.GUEST, adapters)
