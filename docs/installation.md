# Installation and update — v2026.02.01

Use the complete installer and CF wizard from [v2026.02.01](https://github.com/dk-1983/moxa-4vrs-gateway/releases/tag/v2026.02.01). UC-7420-LX Plus vendor firmware 1.6 is required. This installs the application on CF; it does not upgrade the vendor OS.

1. [Prepare new CF or stage an update using the Ubuntu 24.04 wizard](cf-wizard.md). Full erase removes previous data; update preserves the active installation and RNG.
2. [Install, perform separate initial activation and verify](release-final-commissioning.md). Already active consistent schema 3 needs no repeated activation.
3. [Check the installer result](release-final-commissioning.md): launching a background operation does not establish completion.
4. [Open the LCD/Web user guide](user-guide.md) and verify actual requests to connected instruments.

Old v2026.01.01 commands and hashes do not apply to this package. Check schema 3 compatibility before rollback; restoring a complete older CF image with RNG is unsupported. [Qualified scope and limits](release-final.md).
