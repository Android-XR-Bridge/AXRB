import importlib.util
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
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


class ReleaseChecksumTests(unittest.TestCase):
    def test_both_packages_follow_the_finalized_shared_source_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            release = root / 'out/releases'
            release.mkdir(parents=True)
            for name in ('launcher', 'host', 'runtime', 'protocol', 'scripts/build', 'tests'):
                (root / name).mkdir(parents=True)
            (root / 'launcher/package.json').write_text('{"version":"1.2.3"}')
            for name in ('CMakeLists.txt', 'CMakePresets.json', 'README.md', '.gitignore', 'logo_axrb.png'):
                (root / name).write_bytes(b'fixture')
            profile = root / 'launcher/profile'
            profile.mkdir()
            for name in ('meta-session.txt', 'meta-session.bin'):
                (profile / name).write_bytes(b'private session fixture')
            script = root / 'scripts/build/source_archive.py'
            shutil.copyfile(Path(__file__).resolve().parents[2] / 'scripts/build/source_archive.py', script)
            portable = release / 'AXRB-Portable-1.2.3.zip'
            executable = release / 'AXRB-1.2.3.exe'
            source = release / 'AXRB-1.2.3-source.zip'
            portable_manifest = release / 'SHA256SUMS-1.2.3-portable.txt'
            exe_manifest = release / 'SHA256SUMS-1.2.3.txt'

            def finalize():
                subprocess.run([sys.executable, str(script)], check=True, capture_output=True, text=True)

            def verify(manifest, package):
                entries = dict(line.split('  ', 1)[::-1] for line in manifest.read_text().splitlines())
                self.assertEqual(set(entries), {package.name, source.name})
                for file in (package, source):
                    self.assertEqual(entries[file.name], hashlib.sha256(file.read_bytes()).hexdigest())

            portable.write_bytes(b'portable package')
            finalize()
            verify(portable_manifest, portable)
            self.assertFalse(exe_manifest.exists())
            with zipfile.ZipFile(source) as archive:
                self.assertIn('launcher/package.json', archive.namelist())
                for name in ('meta-session.txt', 'meta-session.bin'):
                    self.assertNotIn(f'launcher/profile/{name}', archive.namelist())
            previous_source = source.read_bytes()

            # Building the other mode may regenerate the shared source archive.
            (root / 'launcher/package.json').write_text(json.dumps({'version': '1.2.3', 'description': 'updated'}))
            executable.write_bytes(b'executable package')
            finalize()
            self.assertNotEqual(source.read_bytes(), previous_source)
            verify(portable_manifest, portable)
            verify(exe_manifest, executable)

            # Rebuilding portable must refresh its binary hash and both source entries.
            (root / 'host/new.cpp').write_text('int updated;')
            portable.write_bytes(b'rebuilt portable package')
            finalize()
            verify(portable_manifest, portable)
            verify(exe_manifest, executable)

            # A removed package must not leave a manifest advertising missing files.
            executable.unlink()
            finalize()
            self.assertFalse(exe_manifest.exists())
            verify(portable_manifest, portable)


@unittest.skipUnless(os.name == 'nt', 'Windows PowerShell portable helpers')
class PortableHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.portable = self.directory / 'Moved \u03a9'
        scripts = self.portable / 'resources/runtime/scripts'
        scripts.mkdir(parents=True)
        self.paths = scripts / 'paths.ps1'
        shutil.copyfile(Path(__file__).resolve().parents[2] / 'scripts/paths.ps1', self.paths)
        (self.portable / 'AXRB.portable').touch()
        (self.portable / 'data').mkdir()
        self.profile = self.portable / 'data/library.json'
        self.environment = {**os.environ, 'AXRB_TEST_PYTHON': sys.executable}
        self.environment.pop('AXRB_PORTABLE_ROOT', None)
        self.environment['AXRB_DATA_HOME'] = str(self.directory / 'external-output')
        self.environment['ANDROID_EMULATOR_HOME'] = str(self.directory / 'external-emulator')

    def run_helper(self, body):
        script = self.directory / 'probe.ps1'
        script.write_text(
            "param([string]$Paths)\n$ErrorActionPreference = 'Stop'\n"
            "[Console]::OutputEncoding = [Text.UTF8Encoding]::new()\n. $Paths\n" + body,
            encoding='utf8')
        return subprocess.run(['powershell.exe', '-NoProfile', '-NonInteractive', '-File',
            str(script), '-Paths', str(self.paths)], env=self.environment,
            capture_output=True, text=True, encoding='utf8', timeout=30)

    def test_standalone_helpers_carry_unicode_runtime_and_temporary_files(self):
        old = self.directory / 'Original \u03a9'
        managed = self.portable / 'Custom \u03a9/AXRB Runtime'
        (managed / 'sdk').mkdir(parents=True)
        (managed / 'sdk/probe.txt').write_text('carried SDK')
        self.profile.write_text(json.dumps({'portableRoot': str(old),
            'settings': {'managedDirectory': str(old / 'Custom \u03a9/AXRB Runtime')}}, ensure_ascii=False), encoding='utf8')
        result = self.run_helper("""
$probe = [IO.File]::ReadAllText((Join-Path $env:ANDROID_HOME 'probe.txt'))
$native = [IO.Path]::GetTempFileName()
$python = & $env:AXRB_TEST_PYTHON -c "import tempfile; f=tempfile.NamedTemporaryFile(delete=False); f.write(b'child data'); f.close(); print(f.name)"
if ($LASTEXITCODE -ne 0) { throw 'Python failed' }
$AxrbPortableRoot = [IO.Path]::GetPathRoot($AxrbPortableRoot)
$volumePath = Assert-AxrbPortablePath (Join-Path $AxrbPortableRoot 'AXRB-no-write/output')
@{ probe=$probe; native=$native; python=$python; emulatorHome=$env:ANDROID_EMULATOR_HOME; volumePath=$volumePath } | ConvertTo-Json -Compress
""")
        self.assertEqual(result.returncode, 0, result.stderr)
        observed = json.loads(result.stdout)
        self.assertEqual(observed['probe'], 'carried SDK')
        self.assertEqual(Path(observed['emulatorHome']), managed / 'android')
        for name in ('native', 'python'):
            self.assertEqual(Path(observed[name]).parent, self.portable / 'temp')
            self.assertTrue(Path(observed[name]).is_file())
        self.assertEqual(Path(observed['python']).read_bytes(), b'child data')
        self.assertEqual(Path(observed['volumePath']), Path(self.portable.anchor) / 'AXRB-no-write/output')
        self.assertFalse((self.directory / 'external-output').exists())
        self.assertFalse((self.directory / 'external-emulator').exists())

    def test_standalone_helper_rejects_external_runtime_before_creating_it(self):
        external = self.directory / 'external-runtime'
        self.profile.write_text(json.dumps({'settings': {'managedDirectory': str(external)}}), encoding='utf8')
        result = self.run_helper("throw 'Should not reach runtime work'\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Portable mode keeps output inside its folder', result.stderr)
        self.assertFalse(external.exists())


if __name__ == '__main__': unittest.main()
