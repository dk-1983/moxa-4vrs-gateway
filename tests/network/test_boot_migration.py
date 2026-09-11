"""Offline derivative checks: never execute a vendor script."""
import importlib.util
import unittest
from pathlib import Path
spec = importlib.util.spec_from_file_location("migration", Path(__file__).resolve().parents[2] / "tools/prepare-network-boot-migration.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
class Migration(unittest.TestCase):
    def test_exact_four_calls_only(self):
        source = b"#!/bin/sh\n# /sbin/ifup -a\n/vendor/hook\n /sbin/ifup -a\n/sbin/ifdown -a\n\t/sbin/ifup -a\n/sbin/ifdown -a\nexit 0\n"
        result = module.prepare(source)
        self.assertEqual(result.count(b"--network-vendor-up || exit $?"), 2)
        self.assertEqual(result.count(b"--network-vendor-down || exit $?"), 2)
        self.assertIn(b"# /sbin/ifup -a\n/vendor/hook\n", result)
        restored = result.replace(b"/etc/4vrs-network/gateway-network-recovery --network-vendor-up || exit $?", b"/sbin/ifup -a").replace(b"/etc/4vrs-network/gateway-network-recovery --network-vendor-down || exit $?", b"/sbin/ifdown -a")
        self.assertEqual(restored, source)
    def test_unknown_call_sites_refused(self):
        for source in (b"/sbin/ifup -a\n", b"x"*16385, b"\0"):
            with self.assertRaises(ValueError): module.prepare(source)
if __name__ == "__main__": unittest.main()
