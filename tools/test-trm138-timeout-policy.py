#!/usr/bin/env python3
"""Bounded read-only slave16 -> slave24 -> slave16 gateway policy test."""
import socket
import struct
import sys


def request(tid, unit):
    return struct.pack(">HHHBBHH", tid, 0, 6, unit, 4, 0, 40)


def receive_adu(sock):
    header = receive_exact(sock, 7)
    length = struct.unpack(">H", header[4:6])[0]
    return header + receive_exact(sock, length - 1)


def receive_exact(sock, length):
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise RuntimeError("connection closed before complete response")
        data.extend(chunk)
    return bytes(data)


def check_normal(response, tid):
    assert struct.unpack(">H", response[:2])[0] == tid
    assert response[2:4] == b"\x00\x00"
    assert response[6:9] == b"\x10\x04\x50"
    assert len(response) == 89


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test-trm138-timeout-policy.py HOST PORT")
    with socket.create_connection((sys.argv[1], int(sys.argv[2])), timeout=5) as client:
        client.settimeout(5)
        first = request(0x3001, 16)
        print("MBAP_TX=" + first.hex().upper())
        client.sendall(first)
        response = receive_adu(client)
        print("MBAP_RX=" + response.hex().upper())
        check_normal(response, 0x3001)

        timeout = request(0x3002, 24)
        print("MBAP_TX=" + timeout.hex().upper())
        client.sendall(timeout)
        response = receive_adu(client)
        print("MBAP_RX=" + response.hex().upper())
        assert response == bytes.fromhex("30020000000318840B")

        final = request(0x3003, 16)
        print("MBAP_TX=" + final.hex().upper())
        client.sendall(final)
        response = receive_adu(client)
        print("MBAP_RX=" + response.hex().upper())
        check_normal(response, 0x3003)
    print("TIMEOUT_POLICY_OK normal=2 exception_0B=1 same_connection=1")


if __name__ == "__main__":
    main()
