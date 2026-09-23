from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/emulator'))
from android_runtime_policy import AdbError, ensure_adb_root


class AdbRootTests(unittest.TestCase):
    def test_disconnect_is_accepted_only_after_confirming_root(self):
        run = Mock(side_effect=[subprocess.CalledProcessError(1, ['adb', 'root']), '', '0'])
        ensure_adb_root(run)
        self.assertEqual(run.call_args.args, ('shell', 'id -u'))

    def test_successful_command_with_nonroot_daemon_is_rejected(self):
        run = Mock(side_effect=['', '', '2000'])
        with self.assertRaisesRegex(RuntimeError, 'root access'):
            ensure_adb_root(run)

    def test_adb_failures_keep_what_adb_said(self):
        error = AdbError(1, ['adb', 'shell', 'umount /vendor/lib64/hw'], '', 'umount: /vendor/lib64/hw: Device or resource busy\n')
        self.assertIsInstance(error, subprocess.CalledProcessError)
        self.assertIn('adb said: umount: /vendor/lib64/hw: Device or resource busy', str(error))

    def test_transport_timeout_is_not_swallowed(self):
        run = Mock(side_effect=subprocess.TimeoutExpired(['adb', 'root'], 120))
        with self.assertRaises(subprocess.TimeoutExpired):
            ensure_adb_root(run)


if __name__ == '__main__': unittest.main()
