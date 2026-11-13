<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# macOS build (issue #2)

`.github/workflows/build-macos.yml` uses `macos-latest` (Apple Silicon), the release
preset, Qt 6.9.3 and a macOS 12 deployment target. The retired `macos-14` runner is
no longer used. Manual runs derive the version from CMake and upload test artifacts;
release tags also publish the DMG and SHA-256 checksum. An explicitly supplied
version must match the source version. Qt 6.9.3 removes the obsolete AGL link dependency,
which is absent from the macOS 26 SDK, while retaining macOS 12 as the minimum target.

## Viewport

The shared renderer now requires **OpenGL 4.1 Core / GLSL 410**, which macOS provides.
Buffers, vertex attributes, textures and framebuffers use the bind-based APIs rather
than OpenGL 4.5 direct state access. Instancing, antialiased lines and DYN text, images,
draw-order depth and streaming-buffer orphaning remain available through the existing
`GpuDevice` interface. The same backend runs on Linux and Windows.

On macOS, a GUI-thread timer owns the GL context and draws each frame. Cocoa requires
view attachment, drawable updates and teardown on the main thread. Geometry computation
remains on its worker; Linux and Windows retain the dedicated render thread. A shared
render session keeps the same drawing path and releases GPU resources with a current
context, including when the native window surface has already been destroyed.

CI runs a small GPU pixel test for vertex offsets, multiple attribute bindings,
instance divisors, buffer updates, texture units, alpha, depth, clipping and framebuffer
resize. It then checks window resize/hide/show and captures DYN typing and Tab navigation
in an actual Cocoa viewport. Headless `--check` and `--plot`
run from outside the checkout, with Qt deployment environment overrides removed.
Finally CI mounts the produced DMG and verifies the packaged app's signature and CLI.
Logs and a screenshot are retained as the `macos-verification` artifact.

These checks do not replace interactive testing on a user's Mac. Before calling issue #2
complete, verify launch from Finder after downloading, drawing and DYN entry, pan/zoom,
image attachment, opening/saving drawings and PDF export on Apple Silicon hardware.

## Signing and notarization

`build_app.sh` adds the offscreen plugin **before** running `macdeployqt`, including that
plugin in dependency deployment. `sign_app.sh` then signs Mach-O files and nested bundles
inside-out and verifies the final bundle using `codesign --verify --deep --strict`.
No deployment mutations occur after the final app signature.

Without credentials, the bundle is **ad-hoc signed**, not Developer ID signed or notarized.
Ad-hoc signing gives Apple Silicon valid code signatures but does not establish Gatekeeper
trust. The workflow and included installation notes make that distinction. A "damaged"
message alone does not establish whether a signature, quarantine or another launch check
failed; an OpenGL context failure is a separate runtime diagnostic.

With all required repository secrets, the workflow uses Developer ID signing with the
hardened runtime and a secure timestamp, notarizes and staples the app, builds the DMG,
then signs, notarizes and staples that DMG as well. Partial configuration fails rather
than silently producing an ad-hoc build. Temporary signing credentials are removed in an
`always()` cleanup step.

| Secret | Contents |
|---|---|
| `MACOS_CERT_P12` | Developer ID Application certificate and private key, base64 `.p12` |
| `MACOS_CERT_PASSWORD` | `.p12` password (may be empty for an unencrypted export) |
| `APPLE_ID` | Apple ID used for notarization |
| `APPLE_TEAM_ID` | Apple Developer team ID |
| `APPLE_APP_PASSWORD` | App-specific password for that Apple ID |

The Apple credentials must be provided by the project's Apple Developer account owner;
CI cannot create them. Until a credentialed run succeeds, notarization remains unverified.

## Local build

```sh
cmake --preset release -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0
cmake --build --preset release --target musacad_app musacad_gl_compat --parallel 2
build/release/bin/musacad_gl_compat
QT_ROOT_DIR=~/Qt/6.9.3/macos packaging/macos/build_app.sh 0.9.0 build/release/bin/musacad_app
```

Needs Qt 6 (`macdeployqt`), Ninja and `rsvg-convert` (`brew install ninja librsvg`).
Set `MACOS_SIGN_IDENTITY` to an installed Developer ID identity to sign with it; the
local script defaults to ad-hoc signing. The workflow contains the DMG/notarization steps.

For Linux regression testing of the same API baseline with Mesa:

```sh
MESA_GL_VERSION_OVERRIDE=4.1 MESA_GLSL_VERSION_OVERRIDE=410 build/release/bin/musacad_gl_compat
MESA_GL_VERSION_OVERRIDE=4.1 MESA_GLSL_VERSION_OVERRIDE=410 build/release/bin/musacad_render_offscreen 10000
```

References: [GitHub runner labels](https://docs.github.com/en/actions/how-tos/write-workflows/choose-where-workflows-run/choose-the-runner-for-a-job),
[Qt deployment](https://doc.qt.io/archives/qt-6.9/macos-deployment.html),
[Apple signing](https://developer.apple.com/documentation/xcode/creating-distribution-signed-code-for-the-mac).
