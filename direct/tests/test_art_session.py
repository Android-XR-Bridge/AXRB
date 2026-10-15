"""Launcher preparation regressions; no JDK, SDK or device is needed."""
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_art_session import stage_libraries, prepare


class ArtSessionTest(unittest.TestCase):
    def test_package_path_is_shared_by_apk_libraries_and_bootstrap(self):
        with patch('prepare_art_session.required', side_effect=lambda p: Path(p).resolve()), \
             patch('prepare_art_session.boot_classpath', return_value='boot'), \
             patch('prepare_art_session.stage_libraries', return_value=Path('libraries')):
            args = prepare(apk='game.apk', activity='com.example.game/.Main', sysroot='root',
                           data='data', bootstrap='boot.dex', native='native', proxies='proxies')
            root = '/data/app/com.example.game-axrb'
            self.assertEqual(args[-2:], [root + '/base.apk', 'com.example.game.Main'])
            mounts = [args[i + 1].split('=', 1)[0] for i, a in enumerate(args) if a == '--mount-ro']
            self.assertIn(root + '/base.apk', mounts)
            self.assertIn(root + '/lib/arm64', mounts)
            self.assertIn(root + '/lib/arm64/libopenxr_runtime.so', mounts)
            self.assertIn('/data/app/axrb/lib/arm64/libopenxr_runtime.so', mounts)
            with self.assertRaises(ValueError):
                prepare(apk='game.apk', activity='../escape/.Main', sysroot='root',
                        data='data', bootstrap='boot.dex', native='native', proxies='proxies')

    def test_legacy_oculus_compatibility_is_opt_in(self):
        def run(**options):
            with patch('prepare_art_session.required', side_effect=lambda p: Path(p).resolve()), \
                 patch('prepare_art_session.boot_classpath', return_value='boot'), \
                 patch('prepare_art_session.stage_libraries', return_value=Path('libraries')):
                args = prepare(apk='game.apk', activity='com.camouflaj.salmon/.Main', sysroot='root', data='data',
                               bootstrap='boot.dex', native='native', proxies='proxies', **options)
            mounts = dict(args[i + 1].split('=', 1) for i, a in enumerate(args) if a == '--mount-ro')
            props = dict(args[i + 1].split('=', 1) for i, a in enumerate(args) if a == '--prop')
            return mounts, props
        mounts, props = run()
        self.assertFalse(any('systemdriver' in m for m in mounts))
        self.assertNotIn('ro.axrb.legacy_oculus_driver', props)
        self.assertNotIn('ro.product.manufacturer', props)

        mounts, props = run(legacy_oculus_driver='driver.apk', quest_identity=True, controllers_only=True)
        root = '/data/app/com.oculus.systemdriver-axrb'
        self.assertEqual(Path(mounts[root + '/base.apk']), Path('driver.apk').resolve())
        self.assertIn(root + '/lib/arm64', mounts)
        # The driver loads the session's runtime, not a copy from another build.
        self.assertEqual(Path(mounts[root + '/lib/arm64/libopenxr_runtime.so']),
                         Path(mounts['/data/app/axrb/lib/arm64/libopenxr_runtime.so']))
        self.assertEqual(props['ro.axrb.legacy_oculus_driver'], root + '/base.apk')
        self.assertEqual(props['persist.sys.axrb.legacy.controllers_only.com.camouflaj.salmon'], '1')
        self.assertEqual((props['ro.product.manufacturer'], props['ro.product.vendor.manufacturer'],
                          props['ro.product.brand'], props['ro.product.model']), ('Oculus', 'Oculus', 'oculus', 'Quest 3'))
        with self.assertRaises(ValueError):
            run(controllers_only=True)

    def test_equal_size_library_update_does_not_reuse_old_code(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            apk = root / 'game.apk'
            for value in (b'old!', b'new!'):
                with zipfile.ZipFile(apk, 'w') as archive:
                    archive.writestr('lib/arm64-v8a/libgame.so', value)
                libraries = stage_libraries(apk, root / 'data')
                self.assertEqual((libraries / 'libgame.so').read_bytes(), value)
                self.assertEqual(stage_libraries(apk, root / 'data'), libraries)

    def test_apk_paths_cannot_escape_extraction(self):
        for name in ('../escape.so', 'a/b.so', 'C:stream', 'x\\evil.so'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as root:
                root = Path(root)
                apk = root / 'game.apk'
                with zipfile.ZipFile(apk, 'w') as archive:
                    archive.writestr('lib/arm64-v8a/' + name, b'bad')
                with self.assertRaises(ValueError):
                    stage_libraries(apk, root / 'data')
                self.assertFalse((root / 'escape.so').exists())

    def test_failed_zip_extract_cannot_publish_complete_receipt(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            apk = root / 'game.apk'
            with zipfile.ZipFile(apk, 'w', compression=zipfile.ZIP_STORED) as archive:
                archive.writestr('lib/arm64-v8a/libgame.so', b'payload123')
            apk.write_bytes(apk.read_bytes().replace(b'payload123', b'corrupt123'))
            with self.assertRaises(zipfile.BadZipFile):
                stage_libraries(apk, root / 'data')
            self.assertFalse(list(root.rglob('.complete.json')))


if __name__ == '__main__':
    unittest.main()
