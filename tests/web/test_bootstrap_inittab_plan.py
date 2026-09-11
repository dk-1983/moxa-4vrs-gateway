import importlib.util
from pathlib import Path
import unittest
spec=importlib.util.spec_from_file_location('plan',Path(__file__).resolve().parents[2]/'tools/bootstrap-inittab-plan.py')
plan=importlib.util.module_from_spec(spec);spec.loader.exec_module(plan)
class Tests(unittest.TestCase):
 def test_roundtrip_preserves_all_other_entries(self):
  data=b'# public fixture\ncon:2345:respawn:/bin/sh --login\n'+plan.ORIGINAL+b'\nmp:2345:once:/etc/init.d/moxamptest\n'
  held=plan.transform(data)
  self.assertEqual(held,data.replace(plan.ORIGINAL,plan.HELD))
  self.assertEqual(plan.transform(held,True),data)
 def test_duplicate_refused(self):
  with self.assertRaises(ValueError):plan.transform((plan.ORIGINAL+b'\n')*2)
 def test_changed_action_refused(self):
  for action in [b'off',b'once',b'wait']:
   with self.assertRaises(ValueError):plan.transform(plan.ORIGINAL.replace(b'respawn',action)+b'\n')
 def test_unknown_command_or_levels_refused(self):
  for data in [plan.ORIGINAL.replace(b'115200',b'9600'),plan.ORIGINAL.replace(b'2345',b'3'),b'# absent']:
   with self.assertRaises(ValueError):plan.transform(data+b'\n')
 def test_no_normalization(self):
  for ending in [b'\r\n',b'',b'\0\n']:
   with self.assertRaises(ValueError):plan.transform(plan.ORIGINAL+ending)
if __name__=='__main__':unittest.main()
