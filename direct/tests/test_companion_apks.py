import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_art_session import prepare

class CompanionApkTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        for name in ['sysroot/system/bin/app_process64','bootstrap.dex',
                     *['native/'+n for n in ('libandroid.so','libaxrbndk.so','libmediandk.so','libaxrbmndk.so','libaxrb_art_bridge.so','libOpenSLES.so')],
                     *['proxies/'+n for n in ('libvulkan.so','libaaudio.so','libEGL.so','libGLESv1_CM.so','libGLESv2.so','libopenxr_runtime.so')]]:
            target=self.root/name; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(b'fixture')
        self.game=self.apk('game',b'game')
        self.first=self.apk('first',b'first')
        self.second=self.apk('second',b'second')
    def tearDown(self):
        self.temporary.cleanup()
    def apk(self,name,value):
        path=self.root/(name+'.apk')
        with zipfile.ZipFile(path,'w') as archive: archive.writestr('lib/arm64-v8a/libshared.so',value)
        return path
    def prepare(self,**options):
        with patch('prepare_art_session.boot_classpath',return_value='fixture'):
            return prepare(apk=self.game,activity='com.example.game/.Main',sysroot=self.root/'sysroot',
                data=self.root/'data',bootstrap=self.root/'bootstrap.dex',native=self.root/'native',proxies=self.root/'proxies',**options)
    def test_companion_libraries_are_separate_and_read_only(self):
        original=self.game.read_bytes()
        args=self.prepare(companion_apks=[self.first,self.second])
        mounts=dict(args[i+1].split('=',1) for i,arg in enumerate(args) if arg=='--mount-ro')
        props=dict(args[i+1].split('=',1) for i,arg in enumerate(args) if arg=='--prop')
        self.assertEqual(props['ro.axrb.companion_count'],'2')
        for index,value in enumerate((b'first',b'second')):
            guest=f'/data/app/axrb-companion-{index}'
            self.assertEqual(props[f'ro.axrb.companion_apk.{index}'],guest+'/base.apk')
            self.assertEqual((Path(mounts[guest+'/lib/arm64'])/'libshared.so').read_bytes(),value)
        self.assertNotIn('--mount-rw',args)
        self.assertEqual(self.game.read_bytes(),original)
    def test_empty_companion_configuration_keeps_original_session(self):
        self.assertEqual(self.prepare(),self.prepare(companion_apks=[]))
    def test_duplicates_game_driver_missing_and_excess_are_rejected(self):
        driver=self.apk('driver',b'driver')
        cases=[dict(companion_apks=[self.first,self.first]),dict(companion_apks=[self.game]),
               dict(companion_apks=[driver],legacy_oculus_driver=driver),
               dict(companion_apks=[self.root/'missing.apk']),dict(companion_apks=[self.first]*9)]
        for options in cases:
            with self.subTest(options=options),self.assertRaises(ValueError): self.prepare(**options)
if __name__=='__main__': unittest.main()
