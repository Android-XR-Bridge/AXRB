from pathlib import Path
import subprocess
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/emulator'))
from performance_policy import apply_performance_policy


class PerformancePolicyTests(unittest.TestCase):
    def guest(self, hardware='ranchu', bridge='libndk_translation.so', fail_kernel=False):
        calls = []
        def run(*args):
            command = args[1]
            calls.append(command)
            if command == 'getprop ro.hardware': return hardware
            if command == 'getprop ro.dalvik.vm.native.bridge': return bridge
            if command == 'getprop berberis.gearup_min_insns': return '0'
            if 'sched/features' in command:
                if fail_kernel: raise subprocess.CalledProcessError(1, args)
                return 'NO_TTWU_QUEUE OTHER_FEATURE'
            if '/proc/swaps' in command:
                if fail_kernel: raise subprocess.TimeoutExpired(args, 120)
                return 'active'
            return ''
        return run, calls

    def test_physical_device_is_not_modified(self):
        run, calls = self.guest(hardware='qcom')
        self.assertEqual(apply_performance_policy(run), {'status': 'skipped'})
        self.assertEqual(len(calls), 1)

    def test_stock_translator_does_not_receive_digitalis_properties(self):
        run, calls = self.guest()
        result = apply_performance_policy(run)
        self.assertEqual(result['scheduler'], 'NO_TTWU_QUEUE')
        self.assertFalse(any('setprop berberis' in c for c in calls))

    def test_digitalis_gears_up_small_hot_regions(self):
        run, calls = self.guest(bridge='libberberis_arm64.so')
        self.assertEqual(apply_performance_policy(run)['gearup_min_insns'], '0')
        self.assertIn('setprop berberis.gearup_min_insns 0', calls)

    def test_missing_kernel_features_do_not_block_launch(self):
        run, _ = self.guest(fail_kernel=True)
        result = apply_performance_policy(run)
        self.assertEqual(result['scheduler']['status'], 'unavailable')
        self.assertEqual(result['swap']['status'], 'unavailable')


if __name__ == '__main__': unittest.main()
