<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# macOS build (issue #2)

`.github/workflows/build-macos.yml` builds Musa CAD on an Apple Silicon runner with Qt 6,
assembles `MusaCAD.app` (`build_app.sh`: the binary, `Info.plist.in`, an `.icns` rendered
from the SVG logo, `macdeployqt`, plus the offscreen platform plugin so the command line
works headless), smoke-tests `--check` and `--plot`, signs and notarizes when the secrets
exist, and uploads `MusaCAD-<ver>-arm64.dmg`. A pushed release tag publishes it to the
GitHub release like the other platforms; a manual run only builds.

## Why it is not a release artifact yet

The viewport renders through **OpenGL 4.5 Core** (direct state access, the render thread's
persistent buffers). macOS stops at OpenGL 4.1 and deprecates it, so the GUI cannot create
its context there. On such a system the app shows what it needs at startup (the OpenGL
version it found, the 4.5 it requires, and that on macOS a Metal or OpenGL 4.1 render
backend is the missing piece) and exits with code 2; the bundle's command line
(`musacad --check`, `musacad --plot`) works, which is what the workflow verifies. Desktop
support on macOS needs that second backend -- Metal (through Qt's RHI or directly), or a
reduced OpenGL 4.1 path without DSA -- behind the existing `GpuDevice` / `GpuCommandBuffer`
seam. That work is tracked on issue #2.

## Signing and notarization

Optional. Add these repository secrets and the workflow signs with a Developer ID
certificate and notarizes with `notarytool`:

| Secret | Contents |
|---|---|
| `MACOS_CERT_P12` | the Developer ID Application certificate, base64 of the `.p12` |
| `MACOS_CERT_PASSWORD` | the `.p12` password |
| `APPLE_ID` | the Apple ID used for notarization |
| `APPLE_TEAM_ID` | the team id |
| `APPLE_APP_PASSWORD` | an app-specific password for that Apple ID |

Without them the `.dmg` is unsigned: Gatekeeper asks once (right-click ▸ Open).

## Local build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZERS=OFF -DENABLE_TSAN=OFF
cmake --build build --target musacad_app
QT_ROOT_DIR=~/Qt/6.8.1/macos packaging/macos/build_app.sh 0.4.0 build/bin/musacad_app
hdiutil create -volname "Musa CAD" -srcfolder MusaCAD.app -ov -format UDZO MusaCAD-0.4.0-arm64.dmg
```

Needs Qt 6 (`macdeployqt`), Ninja and, for the icon, `rsvg-convert` (`brew install ninja librsvg`).
