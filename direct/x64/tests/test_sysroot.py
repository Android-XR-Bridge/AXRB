import io
import pathlib
import sys
import unittest
import zipfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from prepare_sysroot import module_name,open_apex

def archive(**files):
    output=io.BytesIO()
    with zipfile.ZipFile(output,'w') as z:
        for name,data in files.items():z.writestr(name,data)
    return output.getvalue()

class ApexTests(unittest.TestCase):
    def test_canonical_module_name(self):
        self.assertEqual(module_name(b'\x10\x01\x0a\x0fcom.android.art'),'com.android.art')
    def test_compressed_container_detected_by_content(self):
        inner=archive(**{'apex_payload.img':b'payload','apex_manifest.pb':b'\x0a\x0fcom.android.art'})
        with open_apex(archive(original_apex=inner)) as z:
            self.assertEqual(z.read('apex_payload.img'),b'payload')
    def test_bad_names_and_truncation_rejected(self):
        for data in (b'\x0a\x03../',b'\x0a\x20short',b'\x80'*12,b''):
            with self.assertRaises(ValueError):module_name(data)
    def test_missing_payload_rejected(self):
        with self.assertRaises(ValueError):open_apex(archive(other=b'none'))

if __name__=='__main__':unittest.main()
