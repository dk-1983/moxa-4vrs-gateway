#!/usr/bin/env python3
"""Read-only HIL probe; target must already run the selected RAW transport.

Usage: test-raw-trm138-read.py tcp|udp HOST PORT [COUNT] [UNIT=8]
Does not change serial/network configuration, upload files, or manage processes.
UDP packets are accumulated because RAW packetization is not Modbus framing.
"""
import socket
import struct
import sys
import time


def crc(data):
    result = 0xffff
    for byte in data:
        result ^= byte
        for _ in range(8):
            result = (result >> 1) ^ 0xa001 if result & 1 else result >> 1
    return struct.pack('<H', result)


def main():
    mode, host, port = sys.argv[1:4]
    if mode not in ('tcp', 'udp'):
        raise SystemExit('mode must be tcp or udp')
    count = int(sys.argv[4]) if len(sys.argv) > 4 else 100
    unit = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    if not 1 <= count <= 10000 or not 1 <= unit <= 247:
        raise SystemExit('invalid count or unit')
    request = struct.pack('>BBHH', unit, 4, 0, 40)
    request += crc(request)
    address = (socket.gethostbyname(host), int(port))
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM if mode == 'tcp' else socket.SOCK_DGRAM) as s:
        s.settimeout(3)
        s.connect(address)
        for sequence in range(count):
            s.sendall(request)
            response = b''
            deadline = time.monotonic() + 3
            while len(response) < 85:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f'poll {sequence}: partial response {response.hex()}')
                s.settimeout(remaining)
                chunk = s.recv(1025)
                if not chunk:
                    raise RuntimeError('connection closed')
                response += chunk
            assert len(response) == 85, response.hex()
            assert response[:3] == bytes((unit, 4, 80)), response.hex()
            assert crc(response[:-2]) == response[-2:], response.hex()
            if sequence == 0:
                print('RTU_RX=' + response.hex())
    print(f'RAW_{mode.upper()} unit={unit} successful_reads={count}')


if __name__ == '__main__':
    main()
