# Version policy

English · [Русский](versioning.ru.md)

4VRS Gateway uses `vYEAR.RELEASE.PATCH`. This is a calendar-based project
convention, not Semantic Versioning.

- YEAR: four-digit year in which the feature release is first published.
- RELEASE: feature-release sequence within that year, starting at `00`.
- PATCH: correction sequence, normally starting at `00`.

RELEASE and PATCH use at least two decimal digits. A new feature release
increments RELEASE and resets PATCH to `00`. A correction increments PATCH
without changing YEAR or RELEASE. A new year's first feature release starts
at `vYEAR.00.00`; fixes to an older release retain its year.

## First public release exception

The first public release is **v2026.00.01**. This one-time exception retains
the version already embedded in the tested development build. It is the first
public release, not a correction to a published v2026.00.00. No v2026.00.00
release is required. Subsequent releases follow the normal sequence below.

| Publication | Version |
| --- | --- |
| First public release | `v2026.00.01` |
| First correction after publication | `v2026.00.02` |
| Next correction | `v2026.00.03` |
| Second feature release in 2026 | `v2026.01.00` |
| First correction to the second feature release | `v2026.01.01` |

The next feature release focuses on an automatic Gateway download and
installation system: develop it, test it, then publish the release. Implementation
details will be defined separately. If published in 2026, its version is
`v2026.01.00`. The web interface is deferred to a later release.

## Build and publication identity

Existing development labels such as r15 are internal build identifiers.
Preserve their hashes and historical reports. No version-only rebuild or
change to `src/version.h` is required for the first public release because it
already reports v2026.00.01. This naming decision does not replace artifact,
installation or hardware validation, and does not itself publish a release.

The product version is independent of the persistent-configuration schema.
Published Git tags and release assets must identify the exact tested source
and binary; include its SHA256. Do not reuse or move a published version tag.
