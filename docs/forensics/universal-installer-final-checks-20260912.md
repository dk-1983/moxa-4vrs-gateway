# Universal v2026.02.03 candidate — checks on 12 September 2026

This records candidate qualification, not publication. [Detailed Russian report](universal-installer-final-checks-20260912.ru.md).

One archive selects UC-7420-LX Plus OS 1.6/Linux 2.6.10 or UC-7420-LX OS 2.3/Linux 2.4.18 on the device. `sh install.sh --detect` is read-only; `sh install.sh` starts transactional installation. Unknown platforms are rejected.

Linux 2.4 lacks UNIX SOCK_SEQPACKET on the tested device. The network service uses bounded fixed-record STREAM framing and a private inherited DGRAM guardian pair. Linux 2.6 retains SEQPACKET. Peer credentials and protected directories remain enforced. Web IPC uses `/var/4vrs-web-ipc` in RAM on both platforms. Platform-specific interface names, optional Wi-Fi, ifdown syntax, loopback state, Apache paths and ABI are handled. Starting the application after stop also restores its managed network service.

Moxa #3 and #4 passed installation, stop/start and healthy no-op. RNG remained ready/schema 3, generations 21 and 6 respectively. On #4 an OS reboot returned networking, Gateway, RNG and Web. The final recovery-executable-only update and no-op preserved Gateway PID 175, RNG PID 177 and generation 6. Web PID preservation is not claimed. Individual HTTP checks measured 47 ms after #3 restart and 16 ms after #4 final no-op; these are not latency statistics.

An earlier #3 services-stop failure rolled back successfully; its specific cause remains unknown. A subsequent diagnostic build and stop/start/no-op passed. An early #4 status check saw an operation still running; the completed operation returned no-op.

The CF wizard passed 27 normal and 27 UBSan scenarios, including both ELF profiles, ext3 round-trip, state preservation, corrupt files and nested symlink rejection. Normal was repeated with the final archive pin. The distributed ZIP passed actual unzip, both manifests, checksums, worker execution, package loading and tamper rejection. Its repeated build is byte-identical. No physical CF was formatted by the wizard in this series.

Archive SHA256: `3e911f67b2f847b881121863357814a219e340afcabb01913241b48828983d22`.

CF kit SHA256: `90fc5c7c5a7ea8721f41d056a90a0b3ef4c00e82a83078c20585bd21ff53a858`, 4,225,250 bytes.

Power loss during NV writes, NAND endurance and eight loaded physical UART/Modbus ports remain unqualified. These results do not establish lossless Modbus operation. Release-branch integration, overall documentation alignment and final delivery checks remain before publication.
