"""Physical bootstrap client. No COM discovery, retries, payload files or dumps.

Local tests inject a public source and transport; production uses BCrypt only
after preflight, receiver READY/missing-state and explicit `provision` command.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
import zlib

PUBLIC = bytes(255 if i == 5 else i for i in range(32))
MODES = {"probe": b"T", "provision": b"P", "status": b"S"}


class BootstrapError(Exception):
    pass


def exact(io, count, end):
    out = bytearray()
    while len(out) < count:
        if time.monotonic() >= end:
            raise BootstrapError("timeout; run status before any further provision")
        block = io.read(count - len(out))
        if block:
            out.extend(block)
    return out


def send(io, data, end):
    at = 0
    while at < len(data):
        if time.monotonic() >= end:
            raise BootstrapError("write timeout; run status before any further provision")
        n = io.write(memoryview(data)[at:])
        if n < 0 or n > len(data) - at:
            raise BootstrapError("invalid transport result")
        at += n


def system_random_into(view):
    if os.name != "nt":
        raise BootstrapError("production source requires Windows BCryptGenRandom")
    bcrypt = ctypes.WinDLL("bcrypt", use_last_error=True)
    fn = bcrypt.BCryptGenRandom
    fn.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong]
    fn.restype = ctypes.c_long
    buf = (ctypes.c_ubyte * len(view)).from_buffer(view)
    if fn(None, buf, len(view), 2) != 0:  # BCRYPT_USE_SYSTEM_PREFERRED_RNG
        raise BootstrapError("OS random source failed")


def exchange(io, mode, source=system_random_into, timeout=8):
    """No retry, including when helper may already have committed."""
    letter = MODES[mode]
    send(io, b"RBH1" + letter + b"\0\0\0", time.monotonic() + 3)
    response = exact(io, 8, time.monotonic() + 40)
    if response[:5] != b"RBS1" + letter or response[6:] != b"\0\0":
        raise BootstrapError("receiver handshake failed; no secret generated")
    state = response[5]
    if mode == "status":
        return {0: "ready", 3: "requires-provision", 4: "invalid-state",
                5: "policy-required", 6: "CF-unavailable"}.get(state, "receiver-error")
    if state != (3 if mode == "provision" else 0):
        raise BootstrapError("receiver refused: state is not eligible; no secret generated")
    frame = bytearray(44)
    end = time.monotonic() + timeout  # Includes OS RNG time; never send late.
    try:
        frame[:8] = b"RBF1" + letter + b"\0\0\x20"
        if mode == "probe":
            frame[8:40] = PUBLIC
        else:
            source(memoryview(frame)[8:40])
        struct.pack_into(">I", frame, 40, zlib.crc32(memoryview(frame)[:40]))
        send(io, frame, end)
        send(io, b"RBE1DONE", end)
        ack = exact(io, 8, time.monotonic() + 40)
        if ack != b"RBA1\0\0\0\0":
            raise BootstrapError("operation not confirmed; run status, do not repeat provision")
        return "probe-passed" if mode == "probe" else "provision-complete; policy-required"
    finally:
        # Best effort for the owned mutable buffer; Python/driver/runtime copies
        # and paging/crash dumps cannot be securely erased by this assignment.
        frame[:] = b"\0" * len(frame)


class WindowsSerial:
    """Exclusive synchronous Win32 handle, bounded per-call I/O; 115200 8N1."""
    class DCB(ctypes.Structure):
        _fields_ = [("length", ctypes.c_uint32), ("baud", ctypes.c_uint32),
                    ("flags", ctypes.c_uint32), ("reserved", ctypes.c_uint16),
                    ("xonlim", ctypes.c_uint16), ("xofflim", ctypes.c_uint16),
                    ("bytesize", ctypes.c_ubyte), ("parity", ctypes.c_ubyte),
                    ("stopbits", ctypes.c_ubyte), ("xon", ctypes.c_char),
                    ("xoff", ctypes.c_char), ("error", ctypes.c_char),
                    ("eof", ctypes.c_char), ("event", ctypes.c_char),
                    ("reserved1", ctypes.c_uint16)]

    def __init__(self, port):
        if os.name != "nt" or not re.fullmatch(r"COM[1-9][0-9]*", port):
            raise BootstrapError("an explicitly approved Windows COM port is required")
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        signatures = {
            "CreateFileW": ([ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                             ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p], ctypes.c_void_p),
            "CloseHandle": ([ctypes.c_void_p], ctypes.c_int),
            "GetCommState": ([ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
            "SetCommState": ([ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
            "GetCommTimeouts": ([ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
            "SetCommTimeouts": ([ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
            "PurgeComm": ([ctypes.c_void_p, ctypes.c_uint32], ctypes.c_int),
            "ReadFile": ([ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
            "WriteFile": ([ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.c_void_p, ctypes.c_void_p], ctypes.c_int),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k, name); fn.argtypes = args; fn.restype = result
        self.h = self.k.CreateFileW("\\\\.\\" + port, 0xC0000000, 0, None, 3, 0, None)
        if self.h == ctypes.c_void_p(-1).value:
            self.h = None
            raise BootstrapError("cannot acquire exclusive COM handle")
        self.saved = None; self.saved_time = None
        try:
            old = self.DCB(); old.length = ctypes.sizeof(old)
            self.check(self.k.GetCommState(self.h, ctypes.byref(old)))
            self.saved = old
            times = (ctypes.c_uint32 * 5)()
            self.check(self.k.GetCommTimeouts(self.h, times)); self.saved_time = times
            new = self.DCB(); new.length = ctypes.sizeof(new); new.baud = 115200
            new.flags = 1  # binary; no parity, XON/XOFF, CTS/DSR, DTR/RTS handshake
            new.bytesize = 8; new.xon = b"\x11"; new.xoff = b"\x13"
            self.check(self.k.SetCommState(self.h, ctypes.byref(new)))
            self.check(self.k.SetCommTimeouts(self.h, (ctypes.c_uint32 * 5)(0, 0, 100, 0, 100)))
            # New reserved session: discard stale ACK/terminal bytes before the
            # public greeting. No payload has been generated at this point.
            self.check(self.k.PurgeComm(self.h, 15))
        except BaseException:
            self.close(); raise

    @staticmethod
    def check(ok):
        if not ok:
            raise BootstrapError("serial I/O failed; run status before further provision")

    def read(self, n):
        buf = ctypes.create_string_buffer(n); done = ctypes.c_uint32()
        self.check(self.k.ReadFile(self.h, buf, n, ctypes.byref(done), None))
        return buf.raw[:done.value]  # Only nonsecret protocol responses.

    def write(self, view):
        # Mutable payload is passed directly to Win32, without a bytes copy.
        view = memoryview(view)
        buf = ((ctypes.c_ubyte * len(view)).from_buffer_copy(view) if view.readonly
               else (ctypes.c_ubyte * len(view)).from_buffer(view))
        done = ctypes.c_uint32()
        self.check(self.k.WriteFile(self.h, buf, len(view), ctypes.byref(done), None))
        return done.value

    def close(self):
        if self.h is not None:
            ok = True
            if self.saved is not None:
                ok = bool(self.k.SetCommState(self.h, ctypes.byref(self.saved))) and ok
            if self.saved_time is not None:
                ok = bool(self.k.SetCommTimeouts(self.h, self.saved_time)) and ok
            ok = bool(self.k.CloseHandle(self.h)) and ok; self.h = None
            self.check(ok)


def preflight(args):
    doc = json.loads(Path(args.preflight).read_text(encoding="utf-8-sig"))
    for key in ("physical_channel_approved", "console_supervision_reserved", "no_capture_or_other_readers"):
        if doc.get(key) is not True:
            raise BootstrapError("preflight incomplete: " + key)
    if args.mode == "provision" and doc.get("public_probe_passed") is not True:
        raise BootstrapError("public physical probe must pass before provision")
    if doc.get("port") != args.port or not re.fullmatch(r"COM[1-9][0-9]*", args.port):
        raise BootstrapError("COM port differs from approved preflight")
    for key, path in (("receiver_sha256", args.receiver_binary), ("helper_sha256", args.helper_binary)):
        if hashlib.sha256(Path(path).read_bytes()).hexdigest() != doc.get(key):
            raise BootstrapError("service binary hash mismatch")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("mode", choices=MODES)
    for key in ("port", "preflight", "receiver-binary", "helper-binary"):
        p.add_argument("--" + key, required=True)
    args = p.parse_args()
    io = None
    try:
        preflight(args)
        io = WindowsSerial(args.port)
        result = exchange(io, args.mode)
        io.close(); io = None
        print(result)
        return 0
    except (BootstrapError, OSError, ValueError, KeyboardInterrupt):
        print("Bootstrap not confirmed. No automatic retry. Run a fresh receiver/client status session before further provision.", file=sys.stderr)
        return 1
    finally:
        if io is not None:
            try:
                io.close()
            except BootstrapError:
                print("COM restoration failed; inspect the reserved console before reuse.", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
