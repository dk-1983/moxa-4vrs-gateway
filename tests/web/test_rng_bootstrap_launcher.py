"""Runs on Windows without loading Win32 serial or generating any secret."""
import contextlib
import ctypes
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("bootstrap", ROOT / "tools/rng-bootstrap.py")
client = importlib.util.module_from_spec(spec); spec.loader.exec_module(client)


class Wire:
    def __init__(self, mode="P", state=3, ack=b"RBA1\0\0\0\0", fragment=2):
        self.incoming = bytearray(b"RBS1" + mode.encode() + bytes([state, 0, 0]) + ack)
        self.sent = bytearray(); self.fragment = fragment; self.views = []

    def read(self, n):
        count = min(n, self.fragment)
        out = self.incoming[:count]; del self.incoming[:count]
        return out

    def write(self, data):
        self.views.append(data)
        n = min(len(data), self.fragment); self.sent.extend(data[:n]); return n


class LauncherTests(unittest.TestCase):
    def source(self, view):
        self.calls += 1; view[:] = client.PUBLIC

    def setUp(self):
        self.calls = 0

    def test_fragmented_provision_and_clear(self):
        wire = Wire()
        self.assertIn("provision-complete", client.exchange(wire, "provision", self.source))
        self.assertEqual(self.calls, 1)
        self.assertEqual(wire.sent[16:48], client.PUBLIC)
        self.assertEqual(wire.sent[-8:], b"RBE1DONE")
        self.assertTrue(all(not any(v) for v in wire.views if not v.readonly))

    def test_probe_never_generates(self):
        wire = Wire("T", 0)
        self.assertEqual(client.exchange(wire, "probe", self.source), "probe-passed")
        self.assertEqual(self.calls, 0)
        self.assertEqual(wire.sent[16:48], client.PUBLIC)

    def test_status_never_generates(self):
        for state, expected in [(0, "ready"), (3, "requires-provision"), (4, "invalid-state"), (5, "policy-required"), (6, "CF-unavailable")]:
            wire = Wire("S", state)
            self.assertEqual(client.exchange(wire, "status", self.source), expected)
            self.assertEqual(len(wire.sent), 8)
        self.assertEqual(self.calls, 0)

    def test_existing_state_refuses_before_generation(self):
        for state in [0, 4, 5, 6, 7]:
            wire = Wire(state=state)
            with self.assertRaises(client.BootstrapError):
                client.exchange(wire, "provision", self.source)
            self.assertEqual(len(wire.sent), 8)
        self.assertEqual(self.calls, 0)

    def test_bad_ready_no_generation(self):
        wire = Wire("X")
        with self.assertRaises(client.BootstrapError):
            client.exchange(wire, "provision", self.source)
        self.assertEqual(self.calls, 0)

    def test_helper_failure_no_retry_and_clear(self):
        wire = Wire(ack=b"RBA1\x07\0\0\0")
        with self.assertRaises(client.BootstrapError):
            client.exchange(wire, "provision", self.source)
        self.assertEqual(self.calls, 1)
        self.assertEqual(len(wire.sent), 60)
        self.assertTrue(all(not any(v) for v in wire.views if not v.readonly))

    def test_lost_ack_no_retry(self):
        wire = Wire(ack=b"")
        ticks = iter(range(10000))
        with patch.object(client.time, "monotonic", side_effect=lambda: next(ticks) / 10):
            with self.assertRaises(client.BootstrapError):
                client.exchange(wire, "provision", self.source)
        self.assertEqual(self.calls, 1)
        self.assertEqual(len(wire.sent), 60)

    def test_slow_source_never_sends_after_receiver_deadline(self):
        wire = Wire(); tick = [0]
        def delayed(view):
            view[:] = client.PUBLIC; tick[0] = 20
        with patch.object(client.time, 'monotonic', side_effect=lambda: tick[0]):
            with self.assertRaises(client.BootstrapError):
                client.exchange(wire, 'provision', delayed)
        self.assertEqual(len(wire.sent), 8)

    def test_preflight_before_port_open_or_rng(self):
        with tempfile.TemporaryDirectory() as folder:
            doc = Path(folder) / "approval.json"; doc.write_text("{}")
            argv = ["bootstrap", "provision", "--port", "COM999", "--preflight", str(doc),
                    "--receiver-binary", "missing", "--helper-binary", "missing"]
            with patch.object(sys, "argv", argv), patch.object(client, "WindowsSerial") as serial, contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(client.main(), 1)
                serial.assert_not_called()

    def test_complete_preflight_hash_and_probe_gates(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "public"; binary.write_bytes(b"public fixture")
            digest = client.hashlib.sha256(binary.read_bytes()).hexdigest()
            doc = Path(folder) / "approval.json"
            data = dict(physical_channel_approved=True, console_supervision_reserved=True,
                        no_capture_or_other_readers=True, public_probe_passed=True, port="COM999",
                        receiver_sha256=digest, helper_sha256=digest)
            args = types.SimpleNamespace(mode="provision", preflight=str(doc), port="COM999",
                                         receiver_binary=binary, helper_binary=binary)
            doc.write_text(json.dumps(data)); client.preflight(args)
            for key, value in [("public_probe_passed", False), ("port", "COM998"), ("receiver_sha256", "bad")]:
                changed = dict(data); changed[key] = value; doc.write_text(json.dumps(changed))
                with self.assertRaises(client.BootstrapError): client.preflight(args)

    def test_win32_dcb_layout(self):
        self.assertEqual(ctypes.sizeof(client.WindowsSerial.DCB), 28)
        self.assertEqual(client.WindowsSerial.DCB.bytesize.offset, 18)

    def test_win32_adapter_with_fake_api_no_com(self):
        calls = []; captured = bytearray()
        class Function:
            def __init__(self, fn): self.fn = fn
            def __call__(self, *args): return self.fn(*args)
        def get_state(handle, pointer):
            dcb = ctypes.cast(pointer, ctypes.POINTER(client.WindowsSerial.DCB)).contents
            dcb.baud = 9600; dcb.bytesize = 7
            return 1
        def set_state(handle, pointer):
            dcb = ctypes.cast(pointer, ctypes.POINTER(client.WindowsSerial.DCB)).contents
            calls.append(('state', dcb.baud, dcb.bytesize, dcb.flags)); return 1
        def write(handle, buf, count, done, overlapped):
            captured.extend(ctypes.string_at(buf, count))
            ctypes.cast(done, ctypes.POINTER(ctypes.c_uint32))[0] = count; return 1
        def read(handle, buf, count, done, overlapped):
            ctypes.memmove(buf, b'RBA1', 4)
            ctypes.cast(done, ctypes.POINTER(ctypes.c_uint32))[0] = 4; return 1
        api = types.SimpleNamespace(**{name: Function(fn) for name, fn in {
            'CreateFileW': lambda *a: calls.append(('open', a[0], a[2])) or 42,
            'CloseHandle': lambda *a: calls.append(('closed',)) or 1,
            'GetCommState': get_state, 'SetCommState': set_state,
            'GetCommTimeouts': lambda *a: 1, 'SetCommTimeouts': lambda *a: 1,
            'PurgeComm': lambda handle, flags: calls.append(('purge', flags)) or 1,
            'WriteFile': write, 'ReadFile': read}.items()})
        with patch.object(client.os, 'name', 'nt'), patch.object(client.ctypes, 'WinDLL', return_value=api, create=True):
            serial = client.WindowsSerial('COM999')
            self.assertEqual(serial.write(memoryview(bytearray(client.PUBLIC))), 32)
            self.assertEqual(serial.read(8), b'RBA1')
            serial.close()
        self.assertEqual(captured, client.PUBLIC)
        self.assertEqual(calls, [('open', '\\\\.\\COM999', 0), ('state', 115200, 8, 1), ('purge', 15), ('state', 9600, 7, 0), ('closed',)])

    def test_bcrypt_binding_with_public_fake_only(self):
        calls = []
        class PublicBCrypt:
            def __call__(self, handle, buf, count, flags):
                calls.append((handle, count, flags)); ctypes.memmove(buf, client.PUBLIC, count); return 0
        api = types.SimpleNamespace(BCryptGenRandom=PublicBCrypt())
        output = bytearray(32)
        with patch.object(client.os, 'name', 'nt'), patch.object(client.ctypes, 'WinDLL', return_value=api, create=True):
            client.system_random_into(memoryview(output))
        self.assertEqual(output, client.PUBLIC)
        self.assertEqual(calls, [(None, 32, 2)])

    def test_no_payload_logs(self):
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            client.exchange(Wire(), "provision", self.source)
        self.assertEqual(output.getvalue(), "")


if __name__ == "__main__":
    unittest.main()
