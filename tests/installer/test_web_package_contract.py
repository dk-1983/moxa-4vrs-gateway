import copy
import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('contract',Path(__file__).resolve().parents[2]/'tools/web-package-contract.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
def manifest():
    return {'format':3,'product':'4VRS Gateway','version':'v2026.02.01','entrypoint':'4vrs-install',
            'files':[{'name':n,'size':1,'mode':'0644' if n=='4vrs-web-ui' else '0755','sha256':'a'*64} for n in m.NAMES]}
class Contract(unittest.TestCase):
    def test_envelope(self):
        self.assertTrue(m.validate(manifest()))
        for key,value in [('format',1),('format',True),('version','v2026.01.01'),('entrypoint','sh')]:
            c=manifest();c[key]=value
            with self.assertRaises(ValueError):m.validate(c)
    def test_names(self):
        for name in ('../key','/etc/passwd','4vrs-install','extra','security/key.pem'):
            c=manifest();c['files'][-1]['name']=name
            with self.assertRaises(ValueError):m.validate(c)
    def test_shape_bounds(self):
        for key,value in [('size',0),('size',True),('size',2**32),('mode','0777'),('sha256','x'*64),('link','target')]:
            c=manifest();c['files'][-1][key]=value
            with self.assertRaises(ValueError):m.validate(c)
        c=manifest();c['files'].append(copy.deepcopy(c['files'][0]))
        with self.assertRaises(ValueError):m.validate(c)
    def test_old_default(self):
        self.assertEqual(m.upgrade('v2026.01.01'),{'enabled':True,'certificate_identity':None,'generate_certificate':False})
    def test_preserve(self):
        for enabled in (True,False):
            self.assertEqual(m.upgrade('v2026.02.01',enabled,'unique-key')['certificate_identity'],'unique-key')
            self.assertEqual(m.upgrade('v2026.02.01',enabled,'unique-key')['enabled'],enabled)
    def test_recovery(self):
        previous=('old-binary','old-assets','enabled','old-key','old-startup','schema1')
        candidate=('new-binary','new-assets','enabled','old-key','new-startup','schema2')
        for phase in ('prepared','activated'):
            self.assertEqual(m.recovery(phase,previous,candidate)[0],previous)
        self.assertEqual(m.recovery('committed',previous,candidate),(candidate,()))
        self.assertEqual(m.recovery('prepared',candidate,candidate,True),(candidate,()))
if __name__=='__main__':unittest.main()
