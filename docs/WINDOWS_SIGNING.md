<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Windows signing

The Windows workflow builds an NSIS installer and publishes its SHA-256 checksum.
It does not currently include a code-signing step. For installation help, see the
[Windows guide](https://wiki.musacad.org/get-started/install-windows/).

## Current updater checks

`src/ui/update_installer.cpp` checks the downloaded installer's version and size,
and verifies the published SHA-256 when available. If the running application has
a trusted Authenticode signature, it also requires a valid signature on the
installer before running it. An unsigned build does not enable that last check.

A checksum helps detect a mismatched download; it does not identify the publisher.
The release notes should state whether the installer is signed and which Windows
runtime checks were completed.

## Adding signing to the release workflow

When a signing service or certificate is available:

1. Sign the application executables before packaging them.
2. Include the NSIS uninstaller in the signing plan, then sign the finished installer.
3. Verify the signatures in the Windows build before publishing.
4. Calculate the release checksum after signing, because signing changes the file.
5. Test installing, uninstalling and updating on Windows, including an update from
   the previous unsigned release.

Keep credentials in the signing service or CI secrets. Choose and configure the
service against its current documentation before adding a workflow step.

The [earlier signing proposal](https://github.com/MusaCAD/MusaCAD/blob/bf479901fecccf3ed92ca2158648fd320156c0b9/docs/WINDOWS_SIGNING.md)
is retained in git history. Its provider prices, eligibility details and suggested
workflow are not current release instructions.
