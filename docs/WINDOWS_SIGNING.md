<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Windows: the "unknown publisher" prompts, and the plan to end them

Every Windows user who downloads `MusaCAD-<version>-x86_64-setup.exe` meets two or three
warnings before Musa CAD runs:

1. **The browser** flags the download ("This file isn't commonly downloaded", "Make sure you
   trust it before you open it").
2. **SmartScreen**, on running the setup: "Windows protected your PC -- Microsoft Defender
   SmartScreen prevented an unrecognized app from starting", with "Run anyway" hidden behind
   "More info".
3. **User Account Control**: the setup needs administrator rights (Program Files), and the
   consent prompt says "Publisher: Unknown" on a yellow banner instead of a named publisher
   on a blue one.

All three have one cause: the files are not code-signed, so Windows has no publisher to name
and no reputation to look up. Nothing in Musa CAD is being flagged as malicious; the files are
merely *unknown*. This page is the plan to change that. It is a plan, not a purchase: no
certificate has been bought and nothing here pretends to be signed.

## What signing does and does not do

- **A named publisher.** With any valid code-signing certificate, UAC shows the publisher's
  name on the blue banner and the file's Properties gain a Digital Signatures tab. The
  "Unknown publisher" wording goes away at once.
- **SmartScreen reputation.** SmartScreen keeps a reputation per *file* and per *publisher
  certificate*. A new, unsigned file has none, hence the prompt. A signed file inherits its
  certificate's reputation; a *new* certificate starts at zero and earns reputation as
  people download and run files signed with it -- typically a few weeks and some hundreds of
  installs, faster for files nobody reports. Every release then starts with the reputation
  the certificate already has, instead of from zero like an unsigned file.
- **Extended Validation (EV) certificates** used to get immediate SmartScreen reputation. Since
  2024 Microsoft no longer grants that automatically: EV still starts with *some* standing and
  earns the rest faster, but the instant pass is gone. EV also requires the private key on a
  hardware token or an HSM, which a CI job cannot hold without a cloud signing service.
- **The browser warning** is SmartScreen too (in Edge) or the browser's own download
  reputation (Chrome, Firefox); it follows the same publisher reputation.
- **The in-place update** inside Musa CAD (the Update dialog on Windows) checks the download
  against the release's size, published SHA-256 and version resource today. Once
  `musacad_app.exe` is signed, the updater *requires* a valid Authenticode signature on the
  installer it downloads before it runs it (`UpdateInstaller::self_is_signed` turns that
  check on by itself). Signing therefore also closes the updater's remaining trust gap.

## The options

| Option | Cost | Key custody | SmartScreen | Fit for a CI build |
|---|---|---|---|---|
| **OV certificate** (Sectigo, DigiCert, GlobalSign, SSL.com…) | roughly USD 200-400 / year | since June 2023 the key must live on a FIPS 140-2 token or HSM; the CA ships a USB token, or offers cloud signing for extra | named publisher at once; reputation builds over weeks | only through the CA's cloud signing service or a self-hosted signing machine with the token plugged in |
| **EV certificate** | roughly USD 300-700 / year, plus an identity check of the organisation | hardware token or HSM, same as OV | named publisher at once; starts with some standing, earns the rest | same constraints as OV |
| **Azure Trusted Signing** (Microsoft) | USD 9.99 / month (Basic) | Microsoft's HSM; nothing to ship or lose | Microsoft-issued short-lived certificates; reputation is tied to the *identity* and carries across renewals | first-class: a GitHub Action (`azure/trusted-signing-action`) signs in CI with a service principal |
| **SignPath Foundation** (free code signing for open-source projects) | free | SignPath's HSM | a real OV certificate ("SignPath Foundation, certified by …"), reputation builds like any OV | first-class: a GitHub Action (`signpath/github-action-submit-signing-request`) submits the artifact, SignPath signs and returns it |
| **winget** (Windows Package Manager) | free | none needed for the manifest, but winget requires the installer to be signed or to match a pinned SHA-256; unsigned installers are accepted with the hash and users still meet SmartScreen when the installer runs | none by itself | a manifest PR to `microsoft/winget-pkgs` per release; can be automated with `wingetcreate` |
| **Microsoft Store** (MSIX) | one-time USD 19 developer account | Microsoft signs the package | no SmartScreen prompts at all for Store installs | needs an MSIX packaging step and a Store submission per release; a different installer than the NSIS one |
| **Self-signed certificate** | free | ours | none -- the certificate chains to nothing Windows trusts, so the prompt gets *worse* ("The publisher could not be verified") | pointless for distribution; fine for testing the signing steps |

### Requirements each option puts on the project

- **Any real certificate**: the organisation or person is identity-checked (business
  registration or, for an individual, a notarised ID; SignPath asks for the project's
  open-source status and a maintainer's identity instead). A registered legal name appears
  as the publisher. For an individual developer the publisher becomes the person's name.
- **Azure Trusted Signing**: an Azure subscription, identity validation of the organisation or
  individual (individual validation is offered in a growing list of countries; check that
  India is on it before counting on it), and the signing account in a supported region.
- **SignPath Foundation**: an OSI-approved license (Musa CAD is LGPL-3.0-or-later, fine), a
  public repository with a build the SignPath reviewers can reproduce from CI, one named
  maintainer as the approver of each signing request, and the project's "artifact
  configuration" in their portal. Turnaround for the application is usually a few weeks.

## Recommendation

**Apply to SignPath Foundation first; sign both the installer and the executables; publish
through winget as well.** Reasons:

- It costs nothing, which matches a donation-funded project.
- It is a real OV certificate held in an HSM, so SmartScreen reputation accrues to it release
  after release, and UAC names the publisher.
- Its GitHub Action fits the existing tag-driven workflow: the Windows job builds, submits the
  installer, SignPath signs it, the job downloads the signed file and publishes it.
- winget gets Musa CAD in front of the users who install from the command line, with a
  pinned checksum per version, and its manifests are updated by a bot once set up.

**If SignPath declines or takes too long**, Azure Trusted Signing at USD 9.99 a month is the
fallback: the cheapest real signature, no hardware to keep safe, and a supported GitHub
Action. An OV certificate from a CA is the last resort (the token makes CI signing awkward).
EV is not worth its price for this project since it no longer buys instant reputation.

The Microsoft Store is a separate, later channel: it removes the prompts entirely, but it
means maintaining an MSIX package and a Store listing next to the NSIS installer. Revisit
once the installer is signed and the user base asks for it.

## The steps

1. **Apply** at signpath.org (Foundation → "Apply for OSS signing"), naming the repository,
   the license, the maintainer, and the artifact to sign (`MusaCAD-<version>-x86_64-setup.exe`
   from the `build-windows.yml` workflow).
2. **Sign inside the installer too, not only the installer.** SmartScreen judges the file the
   user runs (the setup), UAC names its publisher, but the *program* is what runs every day
   and what the in-place updater checks. SignPath's artifact configuration can sign nested
   files: sign `musacad_app.exe`, `musacad.exe` and the uninstaller stub, then the setup.
   With NSIS the uninstaller is generated at install time from a stub inside the setup, so
   it is signed through NSIS's `!uninstfinalize` (which hands the stub to a signing command
   at build time) -- with SignPath this is done by a two-stage request, or the uninstaller
   is left unsigned at first (UAC names a publisher only for the setup and the program).
3. **Timestamp** every signature (SignPath and Trusted Signing do; with `signtool`, pass
   `/tr http://timestamp.digicert.com /td sha256`). A timestamped signature stays valid
   after the certificate expires, so old releases keep working.
4. **Wire the workflow** (`.github/workflows/build-windows.yml`), once the certificate exists:

   ```yaml
   # after "Package with NSIS", before the checksum step
   - name: Sign the installer (SignPath)
     uses: signpath/github-action-submit-signing-request@v1
     with:
       api-token: ${{ secrets.SIGNPATH_API_TOKEN }}
       organization-id: ${{ vars.SIGNPATH_ORG_ID }}
       project-slug: musacad
       signing-policy-slug: release-signing
       github-artifact-id: ${{ steps.upload-unsigned.outputs.artifact-id }}
       wait-for-completion: true
       output-artifact-directory: packaging/windows/signed
   ```

   (the unsigned installer is uploaded as an artifact first; the signed one replaces it
   for the checksum and the release upload). For Azure Trusted Signing the equivalent is
   `azure/trusted-signing-action` with `files-folder: packaging/windows` and
   `files-folder-filter: exe`, run *before* packaging for the executables and *after* for
   the setup. The `.sha256` checksum must be computed **after** signing.
5. **Verify** in CI after signing: `signtool verify /pa /v MusaCAD-*-setup.exe` (from the
   Windows SDK) must exit 0, and the release job refuses to publish otherwise.
6. **winget**: after the first signed release, `wingetcreate new` the manifest
   (`MusaCAD.MusaCAD`, installer type `nullsoft`, silent switch `/S`, the setup's URL and
   SHA-256) and open the pull request against `microsoft/winget-pkgs`; later releases use
   `wingetcreate update` from the tag workflow.
7. **Reputation**: expect SmartScreen to keep prompting for the first weeks after the first
   signed release. Do not rotate the certificate or the publisher name afterwards; the
   reputation is bound to them.

## What the project already does that helps

- The setup carries a version resource (product name, version, company, copyright), so the
  SmartScreen and UAC prompts show a named file even while it is unsigned.
- The installed program is a windowed program with a UTF-8 manifest and no console; the
  installer requests administrator rights explicitly (`RequestExecutionLevel admin`) rather
  than relying on Windows guessing from the file name.
- Releases publish a SHA-256 beside the installer, and the in-place updater verifies it,
  the size and the version resource before running anything, and requires a signature as
  soon as the program itself is signed.

## What to tell users meanwhile

The release notes and README can say plainly: the installer is not yet code-signed, Windows
will warn about an unknown publisher, and the SHA-256 published beside the installer is the
way to check the download (PowerShell: `Get-FileHash MusaCAD-<version>-x86_64-setup.exe`).
