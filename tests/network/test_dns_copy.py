"""Offline migration guard tests; no network, device, or resolver mutation."""
import importlib.util
import pathlib
import unittest

path = pathlib.Path(__file__).resolve().parents[2] / 'tools/prepare-moxa2-dns-copy.py'
spec = importlib.util.spec_from_file_location('dns_copy', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Migration(unittest.TestCase):
    doc = (b'# preserved\nauto eth0 eth1 eth2 lo\niface eth0 inet static\n'
           b' address 10.0.2.15\n broadcast 10.0.2.255\n'
           b'\tdns-servers 10.0.0.1 10.0.0.3\n'
           b'iface eth1 inet static\n address 192.168.3.127\n'
           b'iface eth2 inet static\n up /vendor/hook\n')
    resolver = b'# retained\nnameserver 10.0.0.1\nnameserver 10.0.0.3\n'

    def test_only_key_changes(self):
        self.assertEqual(module.normalize(self.doc, self.resolver),
                         self.doc.replace(b'dns-servers', b'dns-nameservers', 1))

    def test_refusals(self):
        cases = [
            (self.doc, self.resolver.replace(b'10.0.0.3', b'10.0.0.4')),
            (self.doc.replace(b'10.0.0.3', b'10.0.0.1'), self.resolver.replace(b'10.0.0.3', b'10.0.0.1')),
            (self.doc.replace(b'10.0.0.3', b'999.0.0.1'), self.resolver.replace(b'10.0.0.3', b'999.0.0.1')),
            (self.doc.replace(b'dns-servers', b'dns-other'), self.resolver),
            (self.doc.replace(b'iface eth1', b' dns-servers 10.0.0.1\niface eth1'), self.resolver),
            (self.doc.replace(b'eth0 inet static', b'eth0 inet dhcp'), self.resolver),
            (self.doc.replace(b'dns-servers', b'dns-nameserver'), self.resolver),
        ]
        for doc, resolver in cases:
            with self.subTest(doc=doc), self.assertRaises(ValueError):
                module.normalize(doc, resolver)


if __name__ == '__main__':
    unittest.main()
