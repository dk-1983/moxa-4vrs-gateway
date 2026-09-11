# Contributing

English · [Русский](CONTRIBUTING.ru.md)

Thank you for contributing to 4VRS Gateway. Report reproducible bugs or propose
changes in [Issues](https://github.com/dk-1983/moxa-4vrs-gateway/issues). For a
security vulnerability, use the private channel in [SECURITY.md](SECURITY.md).

## Bug reports

Include the device model, product version or source commit, transport and
serial settings, expected behavior, actual result and the smallest repeatable
sequence. Distinguish a failed network connection from a Modbus exception or
instrument timeout. Remove credentials and private infrastructure details.

## Source changes

Keep changes focused and explain the resulting behavior. Preserve compatibility
with the historical XScale compiler and target OS. Runtime memory and waits
must remain bounded; unrelated ports should remain usable during recovery.

Run the relevant host tests under `tools/run-host-*-tests.sh` and record their
results. For target-dependent changes, include the XScale build and ABI audit
when available. Describe hardware tests by their actual model, scenario and
result. A host simulation is not a hardware pass; unavailable hardware should
be stated rather than implied to have been tested.

Use synthetic fixtures for reproducible tests. Do not submit vendor firmware,
toolchains, SDK archives, device binaries, full configurations, credentials or
private logs. Obtain vendor build dependencies separately as described in
[THIRD_PARTY.md](THIRD_PARTY.md).

Documentation is English-first, with Russian README and user-guide companions.
Update both languages when changing user-visible behavior. Describe planned
features as planned and keep installation instructions aligned with the actual
release package.
