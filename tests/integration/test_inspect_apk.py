import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('inspect_apk',
    Path(__file__).resolve().parents[2] / 'launcher/inspect_apk.py')
inspect_apk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspect_apk)

PACKAGE = 'com.example.game'
BADGING = "package: name='com.example.game' versionCode='7' versionName='1.0'\napplication-label:'Example'\n"
# aapt2 xmltree format from a MAIN + INFO + Oculus VR manifest with no LAUNCHER.
MANIFEST = '''N: android=http://schemas.android.com/apk/res/android (line=2)
  E: manifest (line=2)
      E: application (line=4)
          E: activity (line=5)
            A: http://schemas.android.com/apk/res/android:name(0x01010003)="com.unity3d.player.UnityPlayerActivity" (Raw: "com.unity3d.player.UnityPlayerActivity")
            A: http://schemas.android.com/apk/res/android:exported(0x01010010)=true
              E: intent-filter (line=6)
                  E: category (line=7)
                    A: http://schemas.android.com/apk/res/android:name(0x01010003)="android.intent.category.INFO"
                  E: category (line=8)
                    A: http://schemas.android.com/apk/res/android:name(0x01010003)="com.oculus.intent.category.VR"
                  E: action (line=9)
                    A: http://schemas.android.com/apk/res/android:name(0x01010003)="android.intent.action.MAIN"
'''


class ApkEntryPointTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        tool = self.root / 'sdk/build-tools/36.1.0/aapt2.exe'
        tool.parent.mkdir(parents=True)
        tool.touch()
        self.apk = self.root / 'base.apk'
        with zipfile.ZipFile(self.apk, 'w'):
            pass

    def inspect(self, manifest=MANIFEST, badging=BADGING, allow_split=False):
        with patch.object(inspect_apk, '_dump', side_effect=lambda tool, kind, *args:
                badging if kind == 'badging' else manifest):
            return inspect_apk.inspect(self.apk, self.root / 'sdk', allow_split)

    def test_vr_and_info_entries_without_phone_launcher_import(self):
        for category in ('android.intent.category.INFO', 'com.oculus.intent.category.VR'):
            other = 'com.oculus.intent.category.VR' if category.endswith('INFO') else 'android.intent.category.INFO'
            with self.subTest(category=category):
                result = self.inspect(MANIFEST.replace(other, 'android.intent.category.DEFAULT'))
                self.assertEqual(result['activity'], PACKAGE + '/com.unity3d.player.UnityPlayerActivity')

    def test_alias_is_selected_and_relative_class_names_are_normalized(self):
        for name in ('.VrEntry', 'VrEntry'):
            with self.subTest(name=name):
                manifest = MANIFEST.replace('E: activity ', 'E: activity-alias ').replace('com.unity3d.player.UnityPlayerActivity', name)
                self.assertEqual(self.inspect(manifest)['activity'], PACKAGE + '/' + PACKAGE + '.VrEntry')

    def test_disabled_or_private_components_are_not_imported(self):
        manifests = [
            MANIFEST.replace('exported(0x01010010)=true', 'exported(0x01010010)=false'),
            MANIFEST.replace('E: activity (line=5)', 'E: activity (line=5)\n            A: http://schemas.android.com/apk/res/android:enabled(0x0101000e)=false'),
            MANIFEST.replace('E: application (line=4)', 'E: application (line=4)\n        A: http://schemas.android.com/apk/res/android:enabled(0x0101000e)=false'),
        ]
        for manifest in manifests:
            with self.subTest(manifest=manifest):
                with self.assertRaisesRegex(RuntimeError, 'launchable'):
                    self.inspect(manifest)

    def test_main_and_category_must_belong_to_same_filter(self):
        manifest = MANIFEST.replace('                  E: action (line=9)',
            '              E: intent-filter (line=9)\n                  E: action (line=10)')
        with self.assertRaisesRegex(RuntimeError, 'launchable'):
            self.inspect(manifest)

    def test_arbitrary_activities_are_not_guessed_as_entry_points(self):
        with self.assertRaisesRegex(RuntimeError, 'launchable'):
            self.inspect(MANIFEST.replace('android.intent.action.MAIN', 'android.intent.action.VIEW'))

    def test_standard_launcher_and_split_contracts_remain_unchanged(self):
        launcher = BADGING + "launchable-activity: name='com.example.game.PhoneEntry'\n"
        self.assertEqual(self.inspect(badging=launcher)['activity'], PACKAGE + '/com.example.game.PhoneEntry')
        split = BADGING.replace("versionCode='7'", "split='config.arm64' versionCode='7'")
        self.assertEqual(self.inspect(badging=split, allow_split=True)['activity'], '')
        with self.assertRaisesRegex(RuntimeError, 'launchable'):
            self.inspect(badging=split)


if __name__ == '__main__':
    unittest.main()
