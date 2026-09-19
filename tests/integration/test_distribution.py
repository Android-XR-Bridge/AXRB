import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('distribution',
    Path(__file__).resolve().parents[2] / 'scripts/emulator/distribution.py')
distribution = importlib.util.module_from_spec(spec)
spec.loader.exec_module(distribution)


class NdkDiscoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def toolchain(self, ndk):
        directory = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
        directory.mkdir(parents=True)
        for driver in ('clang', 'clang++'):
            (directory / f'x86_64-linux-android29-{driver}.cmd').touch()
        return directory

    def test_managed_sdk_finds_latest_development_ndk(self):
        managed = self.root / 'managed/sdk'
        local = self.root / 'local'
        os.environ.update(ANDROID_HOME=str(managed), ANDROID_SDK_ROOT=str(managed), LOCALAPPDATA=str(local))
        ndks = local / 'Android/Sdk/ndk'
        self.toolchain(ndks / '27.3.13750724')
        selected = self.toolchain(ndks / '27.10.14000000')
        self.toolchain(ndks / 'unrelated-directory')
        self.assertEqual(distribution.ndk_compiler(managed), selected / 'x86_64-linux-android29-clang.cmd')

    def test_requested_sdk_takes_precedence_over_other_sdk_roots(self):
        sdk = self.root / 'requested-sdk'
        selected = self.toolchain(sdk / 'ndk/26.1.0')
        other = self.root / 'other-sdk'
        self.toolchain(other / 'ndk/27.3.0')
        os.environ['ANDROID_HOME'] = str(other)
        self.assertEqual(distribution.ndk_compiler(sdk, cxx=True), selected / 'x86_64-linux-android29-clang++.cmd')

    def test_explicit_ndk_is_honored_and_invalid_override_fails(self):
        sdk = self.root / 'sdk'
        self.toolchain(sdk / 'ndk/27.3.0')
        ndk = self.root / 'custom-ndk'
        selected = self.toolchain(ndk)
        os.environ['ANDROID_NDK_HOME'] = str(ndk)
        self.assertEqual(distribution.ndk_compiler(sdk), selected / 'x86_64-linux-android29-clang.cmd')
        os.environ['ANDROID_NDK_HOME'] = str(self.root / 'missing-ndk')
        with self.assertRaises(RuntimeError):
            distribution.ndk_compiler(sdk)

    def test_configured_sdk_fallback_and_missing_compiler(self):
        managed = self.root / 'managed-sdk'
        development = self.root / 'development-sdk'
        os.environ['ANDROID_SDK_ROOT'] = str(development)
        selected = self.toolchain(development / 'ndk/27.3.0')
        driver = selected / 'x86_64-linux-android29-clang++.cmd'
        self.assertEqual(distribution.ndk_compiler(managed, cxx=True), driver)
        driver.unlink()
        with self.assertRaises(RuntimeError):
            distribution.ndk_compiler(managed, cxx=True)


if __name__ == '__main__': unittest.main()
