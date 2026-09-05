# Version policy

English · [Русский](versioning.ru.md)

4VRS Gateway uses `vYEAR.RELEASE.PATCH`, with zero-based release and patch
numbers. This is a calendar-based project convention, not Semantic Versioning.

- YEAR: four-digit year in which the feature release is first published.
- RELEASE: feature-release sequence within that year, starting at `00`.
- PATCH: correction sequence for that feature release, starting at `00`.

RELEASE and PATCH use at least two decimal digits. A new feature release
increments RELEASE and resets PATCH to `00`. A correction to an existing
release increments PATCH without changing YEAR or RELEASE. A new year's first
feature release starts at `vYEAR.00.00`; fixes to an older release retain its year.

| Publication | Version |
| --- | --- |
| First feature release in 2026 | `v2026.00.00` |
| First correction to that release | `v2026.00.01` |
| Second feature release in 2026 | `v2026.01.00` |
| First correction to the second release | `v2026.01.01` |

The first release is planned to include network-interface configuration through
the physical menu. The web interface is planned for the second feature release;
if published in 2026, that release is `v2026.01.00`. These are scope intentions,
not claims that the features are already implemented or accepted.

## Transition from development builds

Existing unpublished development binaries report `v2026.00.01`. That historic
label does not represent a published patch release. Preserve their labels,
hashes and forensic reports. Prepare the first public release as `v2026.00.00`,
updating `src/version.h` and version-dependent checks together during release
preparation, then building and validating the resulting artifact. This policy
change alone does not relabel an installed binary or trigger deployment.

The product version is independent of persistent-configuration schema version.
Published Git tags and release assets must identify the exact tested commit and
binary; include its SHA256. Do not reuse or move a published version tag.
