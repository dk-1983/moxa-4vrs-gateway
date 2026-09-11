#!/usr/bin/env python3
"""Bounded development client for the mock-backed TCP/1502 listener."""
import socket
import struct
import sys
import time


def adu(transaction_id):
    return struct.pack(">HHHBBHH", transaction_id, 0, 6, 1, 3, 1, 1)


def receive_exact(sock, length):
    result = bytearray()
    while len(result) < length:
        chunk = sock.recv(length - len(result))
        if not chunk:
            raise RuntimeError("peer closed before complete response")
        result.extend(chunk)
    return bytes(result)


def connect(host, port):
    sock = socket.create_connection((host, port), timeout=5)
    sock.settimeout(5)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    return sock


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test-modbus-listener.py HOST PORT")
    host, port = sys.argv[1], int(sys.argv[2])
    checks = 0
    with connect(host, port) as client:
        request = adu(0x1001)
        for byte in request:
            client.sendall(bytes((byte,)))
        assert receive_exact(client, len(request)) == request
        checks += 1

        combined = b"".join(adu(value) for value in (0x1002, 0x1003, 0x1004))
        client.sendall(combined)
        assert receive_exact(client, len(combined)) == combined
        checks += 3
        request = adu(0x1005)
        client.sendall(request)
        client.shutdown(socket.SHUT_WR)
        assert receive_exact(client, len(request)) == request
        checks += 1

    clients = [connect(host, port) for _ in range(8)]
    try:
        ninth = connect(host, port)
        time.sleep(0.05)
        try:
            ninth.sendall(adu(0x1FFF))
            assert ninth.recv(1) == b""
        except (ConnectionResetError, BrokenPipeError):
            pass
        ninth.close()
        checks += 1
        for index, client in enumerate(clients):
            client.sendall(adu(0x2000 + index))
        for index, client in enumerate(clients):
            assert receive_exact(client, 12) == adu(0x2000 + index)
            checks += 1

        slow = clients[0]
        slow.sendall(adu(0x3000) + adu(0x3001))
        for index, client in enumerate(clients[1:], 1):
            client.sendall(adu(0x3100 + index))
        for index, client in enumerate(clients[1:], 1):
            assert receive_exact(client, 12) == adu(0x3100 + index)
            checks += 1
        assert receive_exact(slow, 24) == adu(0x3000) + adu(0x3001)
        checks += 2
    finally:
        for client in clients:
            client.close()

    time.sleep(0.1)
    for malformed in (
        b"\x00\x01\x00\x01\x00\x02\x01",
        b"\x00\x01\x00\x00\x00\x00\x01",
        b"\x00\x01\x00\x00\x01\xff\x01",
    ):
        bad = connect(host, port)
        bad.sendall(malformed)
        time.sleep(0.03)
        try:
            assert bad.recv(1) == b""
        except ConnectionResetError:
            pass
        bad.close()
        checks += 1
    with connect(host, port) as client:
        request = adu(0x4001)
        client.sendall(request)
        assert receive_exact(client, len(request)) == request
        checks += 1

    print(f"TARGET_CLIENT_OK checks={checks} fragmented=1 coalesced=3 clients=8 ninth_refused=1 malformed=3 slow_isolated=1 reconnect=1")


if __name__ == "__main__":
    main()
