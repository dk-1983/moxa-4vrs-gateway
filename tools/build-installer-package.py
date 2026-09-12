"""Build a deterministic, project-only installer archive; never contact a device.

This packages a separately built and tested target installer. It does not generate
an installer or establish that installation/rollback is safe on hardware.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import stat
import struct
import tarfile

MAX_BINARY = 8 * 1024 * 1024
MAX_SCRIPT = 64 * 1024
PAYLOAD_NAMES = (
    "4vrs-install", "4vrs-gateway", "4vrs-gateway.init",
    "4vrs-networking-wrapper",
)


def read_regular(path, limit):
    path = Path(path)
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode) or before.st_nlink != 1:
        raise ValueError("input must be a regular, non-symlink file")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags)
    try:
        opened = os.fstat(fd)
        if (not stat.S_ISREG(opened.st_mode) or opened.st_nlink != 1
                or (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino)):
            raise ValueError("input identity changed before open")
        with os.fdopen(fd, "rb", closefd=False) as stream:
            data = stream.read(limit + 1)
        after = os.fstat(fd)
        current = path.lstat()
        signature = lambda value: (value.st_dev, value.st_ino, value.st_size,
                                   value.st_mtime_ns, value.st_ctime_ns)
        if (signature(opened) != signature(after)
                or signature(after) != signature(current)
                or not stat.S_ISREG(current.st_mode)):
            raise ValueError("input changed during read")
    finally:
        os.close(fd)
    if not data or len(data) > limit:
        raise ValueError("input is empty or exceeds size limit")
    return data


def check_target(data, version, platform="linux26"):
    profiles = {"linux26": (0x04000002, b"/lib/ld-linux.so.3\0"), "linux24": (0x202, b"/lib/ld-linux.so.2\0")}
    if platform not in profiles: raise ValueError("unknown platform")
    target_flags, target_loader = profiles[platform]
    """Basic ELF envelope check, not a substitute for the full target ABI audit."""
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x02\x01":
        raise ValueError("expected ELF32 big-endian executable")
    kind, machine, elf_version = struct.unpack_from(">HHI", data, 16)
    phoff = struct.unpack_from(">I", data, 28)[0]
    flags = struct.unpack_from(">I", data, 36)[0]
    ehsize, phsize, phnum = struct.unpack_from(">HHH", data, 40)
    if (kind != 2 or machine != 40 or elf_version != 1 or ehsize != 52
            or flags != target_flags or phsize != 32 or not 1 <= phnum <= 128
            or phoff < 52 or phoff + phnum * phsize > len(data)):
        raise ValueError("unsupported target ELF envelope")
    interpreters = []
    loadable = False
    for index in range(phnum):
        ptype, offset, _, _, filesz, memsz, _, _ = struct.unpack_from(
            ">IIIIIIII", data, phoff + index * phsize)
        if offset + filesz > len(data) or (ptype == 1 and filesz > memsz):
            raise ValueError("truncated or invalid ELF segment")
        if ptype == 1:
            loadable = True
        if ptype == 3:
            interpreters.append(data[offset:offset + filesz])
    if not loadable or interpreters != [target_loader]:
        raise ValueError("unsupported target loader")
    versions = set(re.findall(rb"v[0-9]{4}\.[0-9]{2,}\.[0-9]{2,}\x00", data))
    if versions != {version.encode("ascii") + b"\0"}:
        raise ValueError("embedded version does not match package version")


def check_script(data):
    if (not data.startswith(b"#!/bin/sh\n") or b"\r" in data
            or b"\0" in data or not data.endswith(b"\n")):
        raise ValueError("scripts must use /bin/sh, LF and a final newline")
    data.decode("ascii")


def package(version, installer, gateway, init_script, wrapper, output, *, web=None, license_file=None, rng=None, kdf=None, platform="linux26"):
    if not re.fullmatch(r"v[0-9]{4}\.[0-9]{2,}\.[0-9]{2,}", version):
        raise ValueError("invalid package version")
    if version in ('v2026.02.01', 'v2026.02.03') and web is None:
        raise ValueError('v2026.02.01 requires the format-2 Web payload')
    sources = (installer, gateway, init_script, wrapper)
    names = PAYLOAD_NAMES
    if web is not None:
        if version not in ('v2026.02.01', 'v2026.02.03') or license_file is None:
            raise ValueError('format 2 requires the qualified version and Mbed TLS license')
        if rng is None or kdf is None: raise ValueError('format 3 requires RNG and KDF executables')
        sources += (web,rng,kdf)
        names += ('4vrs-web','4vrs-rng','4vrs-kdf')
    contents = {}
    for index, (name, source) in enumerate(zip(names, sources)):
        binary = index < 2 or index >= 4
        data = read_regular(source, MAX_BINARY if binary else MAX_SCRIPT)
        if binary:
            check_target(data, version, platform)
        else:
            check_script(data)
        contents[name] = data
    metadata = {
        "format": 3 if web is not None else 1,
        "product": "4VRS Gateway",
        "version": version,
        "entrypoint": "4vrs-install",
        "files": [
            {"name": name, "size": len(contents[name]), "mode": "0755",
             "sha256": hashlib.sha256(contents[name]).hexdigest()}
            for name in names
        ],
    }
    contents["manifest.json"] = (json.dumps(metadata, sort_keys=True, indent=2)
                                  + "\n").encode("ascii")
    contents["SHA256SUMS"] = "".join(
        hashlib.sha256(data).hexdigest() + "  " + name + "\n"
        for name, data in sorted(contents.items())
    ).encode("ascii")
    if web is not None:
        contents['LICENSE.mbedtls'] = read_regular(license_file, 65536)
        contents['NOTICE'] = b'4VRS Gateway includes Mbed TLS 3.6.7, used under Apache-2.0. See LICENSE.mbedtls. UI and Help are embedded in 4vrs-web.\n'
    archive = io.BytesIO()
    prefix = "4vrs-gateway-" + version
    with tarfile.open(fileobj=archive, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name, data in sorted(contents.items()):
            info = tarfile.TarInfo(prefix + "/" + name)
            info.size = len(data)
            info.mode = 0o755 if name in names else 0o644
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ""
            tar.addfile(info, io.BytesIO(data))
    compressed = io.BytesIO()
    with gzip.GzipFile(fileobj=compressed, mode="wb", filename="", mtime=0) as gz:
        gz.write(archive.getvalue())
    output = Path(output)
    # Exclusive creation preserves an existing package; failed writes leave no
    # apparently complete archive. All inputs have been validated before this.
    created = False
    try:
        with output.open("xb") as stream:
            created = True
            stream.write(compressed.getvalue())
            stream.flush()
            os.fsync(stream.fileno())
    except BaseException:
        if created:
            output.unlink()
        raise
    return {"version": version, "size": output.stat().st_size,
            "sha256": hashlib.sha256(compressed.getvalue()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("version", "installer", "gateway", "init-script", "wrapper", "output"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument('--platform', choices=('linux26','linux24'), default='linux26')
    parser.add_argument('--web')
    parser.add_argument('--rng')
    parser.add_argument('--kdf')
    parser.add_argument('--license-file')
    args = parser.parse_args()
    try:
        result = package(args.version, args.installer, args.gateway,
                         args.init_script, args.wrapper, args.output,
                         web=args.web, license_file=args.license_file, rng=args.rng, kdf=args.kdf, platform=args.platform)
    except (ValueError, OSError) as error:
        parser.exit(1, "package refused: " + str(error) + "\n")
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
