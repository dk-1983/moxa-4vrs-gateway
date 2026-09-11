"""Package tests use synthetic ELF envelopes, never executable target code."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "installer_package", Path(__file__).resolve().parents[2] / "tools/build-installer-package.py")
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)
VERSION = "v2026.01.01"


def elf(version=VERSION):
    data = bytearray(192)
    data[:7] = b"\x7fELF\x01\x02\x01"
    struct.pack_into(">HHI", data, 16, 2, 40, 1)
    struct.pack_into(">I", data, 28, 52)
    struct.pack_into(">I", data, 36, 0x04000002)
    struct.pack_into(">HHH", data, 40, 52, 32, 2)
    loader = b"/lib/ld-linux.so.3\0"
    struct.pack_into(">IIIIIIII", data, 52, 3, 116, 0, 0, len(loader), len(loader), 4, 1)
    struct.pack_into(">IIIIIIII", data, 84, 1, 0, 0, 0, len(data), len(data), 5, 4096)
    data[116:116 + len(loader)] = loader
    label = version.encode() + b"\0"
    data[160:160 + len(label)] = label
    return bytes(data)


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.inputs = []
        for index, name in enumerate(PACKAGE.PAYLOAD_NAMES):
            p = self.root / name
            p.write_bytes(elf() if index < 2 else b"#!/bin/sh\nexit 0\n")
            self.inputs.append(p)
        self.output = self.root / "bundle.tar.gz"

    def build(self, output=None):
        return PACKAGE.package(VERSION, *self.inputs, output or self.output)

    def test_reproducible_archive_and_verified_allowlist(self):
        first = self.build()
        other = self.root / "second.tar.gz"
        self.assertEqual(first, self.build(other))
        self.assertEqual(self.output.read_bytes(), other.read_bytes())
        with tarfile.open(fileobj=io.BytesIO(other.read_bytes()), mode="r:gz") as archive:
            names = {Path(m.name).name for m in archive.getmembers()}
            self.assertEqual(names, set(PACKAGE.PAYLOAD_NAMES) | {"manifest.json", "SHA256SUMS"})
            for member in archive.getmembers():
                self.assertTrue(member.isfile())
                self.assertEqual(member.uid, 0)
                self.assertEqual(member.mtime, 0)
                self.assertEqual(member.mode, 0o755 if Path(member.name).name in PACKAGE.PAYLOAD_NAMES else 0o644)
            prefix = "4vrs-gateway-" + VERSION + "/"
            manifest = json.load(archive.extractfile(prefix + "manifest.json"))
            self.assertEqual(manifest["entrypoint"], "4vrs-install")
            for entry in manifest["files"]:
                data = archive.extractfile(prefix + entry["name"]).read()
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])
                self.assertEqual(len(data), entry["size"])
            for row in archive.extractfile(prefix + "SHA256SUMS").read().decode().splitlines():
                digest, name = row.split("  ")
                self.assertEqual(hashlib.sha256(archive.extractfile(prefix + name).read()).hexdigest(), digest)

    def test_no_overwrite(self):
        self.output.write_bytes(b"previous release")
        with self.assertRaises(FileExistsError):
            self.build()
        self.assertEqual(self.output.read_bytes(), b"previous release")

    def test_failure_before_output_for_bad_inputs(self):
        cases = [b"", b"not ELF", elf("v2026.00.01")]
        for offset, value in [(4, 2), (5, 1), (19, 3), (36, 5), (42, 1)]:
            data = bytearray(elf()); data[offset] = value; cases.append(bytes(data))
        data = bytearray(elf()); data[116] = ord("X"); cases.append(bytes(data))
        cases.append(elf()[:120])
        for data in cases:
            with self.subTest(size=len(data), start=data[:20]):
                self.inputs[0].write_bytes(data)
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(self.output.exists())

    def test_version_path_injection(self):
        for version in ["../../bad", "v2026.1.0", "v2026.01.01\n", "v2026.01.01/other"]:
            with self.assertRaises(ValueError):
                PACKAGE.package(version, *self.inputs, self.output)
        self.assertFalse(self.output.exists())

    def test_scripts_and_directories_rejected(self):
        for value in [b"#!/bin/bash\n", b"#!/bin/sh\r\n", b"#!/bin/sh\n\0", b"#!/bin/sh\nexit 0"]:
            self.inputs[2].write_bytes(value)
            with self.assertRaises(ValueError):
                self.build()
        self.inputs[2].unlink(); self.inputs[2].mkdir()
        with self.assertRaises(ValueError):
            self.build()
        self.assertFalse(self.output.exists())

    def test_failed_fsync_removes_incomplete_output(self):
        with mock.patch.object(PACKAGE.os, "fsync", side_effect=OSError("injected")):
            with self.assertRaises(OSError):
                self.build()
        self.assertFalse(self.output.exists())

    def test_input_size_bound(self):
        with mock.patch.object(PACKAGE, "MAX_BINARY", 100):
            with self.assertRaises(ValueError):
                self.build()
        self.assertFalse(self.output.exists())

    def test_identity_replacement_between_stat_and_open(self):
        original_open = PACKAGE.os.open
        target = self.inputs[0]
        replacement = self.root / 'replacement'
        replacement.write_bytes(elf())

        def swapped(path, flags, *args, **kwargs):
            if Path(path) == target:
                target.rename(self.root / 'original')
                replacement.rename(target)
            return original_open(path, flags, *args, **kwargs)

        with mock.patch.object(PACKAGE.os, 'open', side_effect=swapped):
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                self.build()
        self.assertFalse(self.output.exists())

    def test_changed_file_during_read(self):
        original_fstat = PACKAGE.os.fstat
        calls = 0

        def changed(fd):
            nonlocal calls
            calls += 1
            if calls == 2:
                with self.inputs[0].open('ab') as stream:
                    stream.write(b'changed')
            return original_fstat(fd)

        with mock.patch.object(PACKAGE.os, 'fstat', side_effect=changed):
            with self.assertRaisesRegex(ValueError, 'changed during read'):
                self.build()
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
