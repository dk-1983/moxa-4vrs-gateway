"""Shell command mechanics in private roots; not real service/first-boot proof."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Commands(unittest.TestCase):
    def test_manual_file_commands(self):
        for optional in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                for path in ('etc/init.d', 'etc/rc.d/rcS.d', 'etc/rc.d/rc3.d',
                             'etc/rc.d/rc0.d', 'etc/rc.d/rc6.d',
                             'var/hda/4vrs/tests/manual-reviewed'):
                    (root/path).mkdir(parents=True, exist_ok=True)
                stage = root/'var/hda/4vrs/tests/manual-reviewed'
                for name in ('4vrs-gateway', 'vendor-networking', 'vendor-networking-managed',
                             'ntpdate', 'ntpdate.d', 'halt', '4vrs-gateway.init', '4vrs-networking-wrapper'):
                    (stage/name).write_text('#!/bin/sh\nexit 0\n')
                original = root/'etc/init.d/networking'
                original.write_text('retained original\n')
                (root/'etc/rc.d/rcS.d/S40networking').symlink_to('../../init.d/networking')
                if optional:
                    (root/'etc/init.d/ntpdate.d').write_text('old\n')
                text=(ROOT/'docs/manual-installation.md').read_text()
                blocks=re.findall(r'```sh\n(.*?)```', text, re.S)
                self.assertGreaterEqual(len(blocks), 8)
                command='set -eu\n'+'\n'.join(blocks)
                command=command.replace('/var/hda',str(root/'var/hda')).replace('/etc/',str(root/'etc')+'/')
                subprocess.run(['/bin/sh','-c',command], check=True, timeout=10)
                self.assertEqual(original.read_text(),'retained original\n')
                self.assertFalse((root/'etc/rc.d/rcS.d/S40networking').is_symlink())
                self.assertEqual((root/'etc/init.d/ntpdate.d').exists(), optional)
                self.assertEqual((root/'etc/rc.d/rc3.d/S90fourvrs-gateway').readlink(),Path('../init.d/4vrs-gateway'))

    def test_observation_wrapper(self):
        for scenario in ('success','failed-child','wrong-hash','symlink','repeat'):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);name='4vrs-installer-qualification-fixture-20260906-03'
                exe=root/name;exe.write_text('#!/bin/sh\nexit '+('7' if scenario=='failed-child' else '0')+'\n');exe.chmod(0o700)
                if scenario=='symlink':
                    exe.rename(root/'real');exe.symlink_to('real')
                if scenario=='repeat':(root/'qualification-result.txt').write_text('preserve\n')
                commands=root/'commands';commands.mkdir()
                # Hash provider is stubbed only in this wrapper test. The target
                # script pins the actual audited ELF hash, not this stub binary.
                script=(ROOT/'tools/run-installer-qualification-fixture.sh').read_text()
                digest=re.search(r" '([0-9a-f]{32})  '",script).group(1)
                stub=commands/'md5sum';stub.write_text('#!/bin/sh\necho "'+('bad' if scenario=='wrong-hash' else digest)+'  file"\n');stub.chmod(0o700)
                result=subprocess.run(['/bin/sh','-c',script],cwd=root,env={**os.environ,'PATH':str(commands)+':'+os.environ['PATH']},capture_output=True,timeout=10)
                self.assertEqual(result.returncode,0 if scenario=='success' else 7 if scenario=='failed-child' else 2,scenario)
                if scenario=='repeat':self.assertEqual((root/'qualification-result.txt').read_text(),'preserve\n')

if __name__ == '__main__':unittest.main()
