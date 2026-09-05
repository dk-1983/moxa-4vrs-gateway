# Compatibility build-environment plan

The official v1.2 toolchain is downloaded, hash-verified, and inspected without
executing its installer. The compatibility environment is defined under
`.devcontainer/` and never copies the vendor payload into its image.

## Likely container

Use the digest-pinned `debian:bookworm-slim` base on linux/amd64 and add the i386 architecture
plus `libc6:i386`. The core compiler, assembler, and linker are ELF32 i386,
use `/lib/ld-linux.so.2`, and the inspected GCC driver needs only `libc.so.6`
with symbol versions no newer than `GLIBC_2.3`. This modern compatibility route
should be tried before a Fedora-era image. If any compiler subprocess exposes
an additional dependency, record it and add only that i386 runtime package.

The image also contains only `file` and host `binutils` for offline ELF
inspection. No compiler, ARM sysroot, network utility, hardware device, or
credential is added.

Docker Desktop's Windows bind filesystem returns inode values that overflow
the historical compiler's 32-bit `stat()` structure. GCC then reports `Value
too large for defined data type`. Consequently, the verified tree is copied
from a read-only bind mount into a Docker named volume. That volume uses the
Linux VM filesystem and is mounted read-only for compiler runs. The toolchain
is still neither included in the image nor Git.

The same i386 `stat()` limitation applies when `cc1` reads source directly
from a Windows bind mount. Project-controlled `src/` and `tools/` are therefore
staged into the disposable `moxa-serial-server-source` volume and mounted
read-only at `/workspace/source`. Generated files remain exclusively in the
separate writable build volume.

## Mount layout

- project source: read-only at `/workspace/source`;
- build output: writable at `/workspace/build`;
- byte-preserving Docker-volume staging copy of the unpacked vendor payload:
  read-only at `/usr/local/xscale_be`, preserving Moxa's historical path;
- target sysroot: `/usr/local/xscale_be/target` in that read-only mount.

Use executable prefix `xscale_be-`; the compiler's target triplet is
`armv5teb-montavista-linuxeabi`. Do not confuse the two.

## Container validation sequence

Before compiling any source:

1. run `file` and `readelf` against the compiler;
2. run `xscale_be-gcc --version`, `-dumpmachine`, `-print-sysroot`, and
   `-print-search-dirs`;
3. run `xscale_be-as --version` and `xscale_be-ld --version`;
4. use `ldd` only on the i386 host tools, never on target ARM artifacts;
5. confirm all reported paths remain inside the read-only vendor mount.

GCC 3.4.3 does not implement `-print-sysroot`; the validation script records
its expected exit 1 and confirms the effective sysroot through `gcc -E -v`.

Run the reproducible diagnostic from the repository root after the image is
built. First populate the disposable toolchain volume from the verified,
read-only vendor bind. The dev-container definition mounts that volume
read-only, disables runtime networking, and applies `no-new-privileges`.

```sh
docker build --platform linux/amd64 -f .devcontainer/Dockerfile \
  -t moxa-xscale-compat:bookworm .

docker volume create moxa-xscale-toolchain

docker run --rm --network none \
  --mount type=bind,src="$PWD/vendor/_audit/toolchain/usr/local/xscale_be",dst=/vendor-source,readonly \
  --mount type=volume,src=moxa-xscale-toolchain,dst=/toolchain \
  moxa-xscale-compat:bookworm \
  /bin/sh -c 'cp -a /vendor-source/. /toolchain/'

docker volume create moxa-serial-server-source

docker run --rm --network none \
  --mount type=bind,src="$PWD",dst=/source-host,readonly \
  --mount type=volume,src=moxa-serial-server-source,dst=/source \
  moxa-xscale-compat:bookworm \
  /bin/sh -c 'cp -a /source-host/src /source-host/tools /source/'

docker run --rm --network none --security-opt no-new-privileges \
  --mount type=volume,src=moxa-xscale-toolchain,dst=/usr/local/xscale_be,readonly \
  --mount type=volume,src=moxa-serial-server-source,dst=/workspace/source,readonly \
  --mount type=volume,src=moxa-serial-server-build,dst=/workspace/build \
  moxa-xscale-compat:bookworm \
  /bin/sh /workspace/source/tools/validate-toolchain.sh
```

## Mandatory validation

Before any deployment, verify with host tools:

- ELF32, ARM, MSB/big-endian;
- EABI version 4 and flags compatible with `0x04000002`;
- interpreter `/lib/ld-linux.so.3`;
- only intended `DT_NEEDED` libraries;
- no symbol requirement absent from the preserved target libraries;
- no accidental ARM hard-float, Thumb-2, ARMv6+, or modern-kernel dependency.

## First acceptance program

The first program performs no hardware I/O. It prints:

```text
4VRS Configuration
v2026.00.01
sizeof(void*)
byte order
uname()
```

and exits zero. The requested `4VRS Configuration` text is treated as an
acceptance-test payload only; this repository remains independent of FourVRS.
Build only after the exact vendor toolchain and sysroot are positively
identified.
