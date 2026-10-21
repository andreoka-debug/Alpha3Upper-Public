import hashlib,json,struct,sys,tempfile,unittest,zlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'launcher'))
from import_game import records,ImportErrorDetail,verify_import,write_manifest

class ImporterBoundaries(unittest.TestCase):
    def archive(self,name,payload,declared=None):
        packed=zlib.compress(payload);row=name.encode().ljust(64,b'\0')+struct.pack('<4I',1,len(packed),len(payload) if declared is None else declared,88)
        return b'ARC\0'+struct.pack('<HH',7,1)+row+packed
    def test_valid_resource(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'test.arc';p.write_bytes(self.archive('sound\\fixture',b'unit-test'))
            self.assertEqual(list(records(p)),[('sound\\fixture',1,b'unit-test')])
    def test_traversal_and_bad_lengths(self):
        for name,payload,length in [('..\\escape',b'x',None),('valid',b'x',200000001),('valid',b'x',2)]:
            with self.subTest(name=name,length=length),tempfile.TemporaryDirectory() as d:
                p=Path(d)/'test.arc';p.write_bytes(self.archive(name,payload,length))
                with self.assertRaises(ImportErrorDetail):list(records(p))
    def test_truncated_arc(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'test.arc';p.write_bytes(self.archive('valid',b'x')[:-2])
            with self.assertRaises(ImportErrorDetail):list(records(p))
    def test_manifest_tamper_and_traversal(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)
            for n in ['main.bin','arena.bin','audio-map.tsv']:(p/n).write_bytes(b'synthetic-fixture')
            write_manifest(p);self.assertTrue(verify_import(p));(p/'arena.bin').write_bytes(b'tampered');self.assertFalse(verify_import(p))
            write_manifest(p);m=json.loads((p/'import.json').read_text());m['files']['../escape']='0'*64;(p/'import.json').write_text(json.dumps(m));self.assertFalse(verify_import(p))
if __name__=='__main__':unittest.main()
