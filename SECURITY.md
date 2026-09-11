# Security policy

English · [Русский](SECURITY.ru.md)

4VRS Gateway is preparing its first public release. There is no published
stable version yet and no security response-time guarantee.

## Reporting a vulnerability

Use GitHub's private vulnerability reporting for this repository:
[Report a vulnerability](https://github.com/dk-1983/moxa-4vrs-gateway/security/advisories/new).
Private reporting is enabled. Do not post exploit details in a public issue
before the report has been reviewed.

Include the affected version or commit, device model, prerequisites,
reproduction steps and observed impact. Prefer a small sanitized reproducer.
Do not include credentials, private keys, full device configurations or vendor
binaries. Coordinate any disclosure and fixes through the private report.

## Deployment boundary

The serial listeners provide protocol transport, not an authentication or
encryption layer. Restrict access to trusted clients using the surrounding
network. The original Moxa operating system and its services are separate
components; installing Gateway does not update or secure that legacy OS.

Ordinary bugs and feature requests belong in
[Issues](https://github.com/dk-1983/moxa-4vrs-gateway/issues).
