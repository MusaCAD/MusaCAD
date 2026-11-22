<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Releasing Musa CAD

Build, verify and publish packages from a versioned source tag. See the
[wiki release guide](https://wiki.musacad.org/developer/releasing/) for an overview.
The workflows on the release tag determine which artifacts are published.

## What gets produced

| Platform | Artifact | Built by |
|---|---|---|
| Linux | `MusaCAD-<ver>-x86_64.AppImage` | `packaging/linux/build_appimage.sh` (local) **and** `.github/workflows/build-linux.yml` (CI) |
| Linux | `MusaCAD-<ver>.flatpak` | `packaging/flatpak/build_flatpak.sh` (local) |
| Windows | `MusaCAD-<ver>-x86_64-setup.exe` | `.github/workflows/build-windows.yml` (CI, `windows-latest`) |
| macOS | `MusaCAD-<ver>-arm64.dmg` + checksum (OpenGL 4.1 viewport; interactive Mac testing required) | `.github/workflows/build-macos.yml` (CI, `macos-latest`) |

Packaging detail lives in `packaging/linux/BUILD_APPIMAGE.md`, `packaging/flatpak/BUILD_FLATPAK.md`
and `packaging/macos/BUILD_MACOS.md`.

## Versioning

- `project(MusaCAD VERSION X.Y.Z ...)` in the top `CMakeLists.txt` is the source of truth.
- Tags are `vX.Y.Z`. On the current branch, a tag triggers the Linux, Windows and
  macOS build workflows. Older tags may have a different workflow set.
- Stable 0.9.0 has Linux and Windows assets. Its Mac build is available separately
  in the `macos-experimental-0.9.0-1` prerelease.

## Pre-flight (do not skip)

1. `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` — all tests green.
2. `cmake --build --preset release` — clean, no warnings.
3. Bump `project(... VERSION ...)` if needed; update `CHANGELOG.md` and write the tracked
   release notes at `docs/release-notes/v<ver>.md`.
4. Re-run the license scan (`docs/THIRD_PARTY_LICENSES.md` → "Reproducing the scan"): no GPL/DWG
   library in the build graph, the binary, or the bundles; the DWG converter stays external.
4b. Add the version's `<release>` entry to `packaging/flatpak/org.musacad.MusaCAD.metainfo.xml`
   (Flathub reads the metainfo from the tagged source; the entry's date is the tag's).
4c. If the interface changed, take the screenshots again: `tools/screenshots/capture.sh`
   (a release build and a desktop session; each picture is made by the app from its own
   widgets and GL frame, so nothing the desktop shows over the window gets in). The
   metainfo's `<screenshot>` URLs point at the commit the pictures landed in, as Flathub
   asks: commit the pictures, then set that commit in the metainfo.
5. Build once inside the Flatpak SDK before tagging. The KDE runtime ships a newer GCC than the
   desktop distributions, and flatpak-builder adds `_GLIBCXX_ASSERTIONS` / `_FORTIFY_SOURCE`, so
   `-Werror` can fire there on code the host compiler accepts (v0.4.0: two null-dereference
   warnings). Quick loop without a full bundle:

   ```bash
   flatpak run --command=bash --filesystem="$PWD" org.kde.Sdk//6.11 -c \
     'cmake -S . -B build/sdk -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_SANITIZERS=OFF \
        -DCMAKE_CXX_FLAGS="-O2 -g -Wp,-D_FORTIFY_SOURCE=3 -Wp,-D_GLIBCXX_ASSERTIONS" && \
      ninja -C build/sdk -k 0'
   ```

## Cut the release

```sh
# Build + verify the Linux artifacts and print the publish commands:
scripts/release.sh <version>          # add --dry-run to build/verify without printing 'go' intent

# Trigger a Windows test build before tagging:
gh workflow run build-windows.yml -f version=<version>
#   ... wait for it, then download the .exe:
gh run download --name "MusaCAD-<version>-x86_64-setup" --dir .
```

Verify locally (the discipline that catches breakage):
- Install + launch the AppImage from outside the build dir; open a drawing, plot a PDF, exit.
- **Exercise the HEADLESS paths from the extracted AppImage too, not just the desktop launch.**
  v0.2.0 shipped a `--plot` mode that forces Qt's `offscreen` platform; the AppImage bundled
  only `libqxcb.so`, so the desktop launch worked perfectly while `--plot` aborted with
  "Could not find the Qt platform plugin \"offscreen\"" and exit code 127. A working GUI does
  **not** imply a working headless mode -- they load different plugins. Minimum check:

  ```sh
  cd "$(mktemp -d)" && cp <repo>/MusaCAD-<ver>-x86_64.AppImage .
  env -u DISPLAY -u WAYLAND_DISPLAY APPIMAGE_EXTRACT_AND_RUN=1 \
      ./MusaCAD-<ver>-x86_64.AppImage --plot <drawing>.musa out.pdf --paper A4
  ```
- Install + launch the Flatpak (`flatpak install --user ...flatpak`; `flatpak run org.musacad.MusaCAD`).
- **Windows:** install the `.exe` on Windows and check launching and drawing.
  Record interactive testing separately from the CI build and command-line checks.
  Check both programs the installer ships: `musacad_app.exe` from the Start menu (no console
  window), and `musacad.exe --check <drawing>` / `--plot` from cmd or PowerShell (waits and
  returns the exit code). Open a drawing from a folder with a non-ASCII name too.

## Tag + publish

Pushing the tag is the release action. The platform workflows build on the tag,
create the GitHub release if needed, and attach their packages and checksums.
Review the workflow files on that tag before publishing.

```sh
git tag -a v<version> -m "Musa CAD v<version>"
git push origin v<version>
```

Once the platform workflows pass, set the notes and attach the Flatpak (built locally):

```sh
gh release edit v<version> \
  --title "Musa CAD v<version>" \
  --notes-file docs/release-notes/v<version>.md
gh release upload v<version> packaging/flatpak/MusaCAD-<version>.flatpak
```

Confirm that the release page has the expected artifacts and checksums, the notes
render correctly, and the `v<version>` tag is visible. State which runtime checks
passed and which are still pending for each platform.

**The Windows installer's checksum.** The Windows workflow publishes
`MusaCAD-<version>-x86_64-setup.exe.sha256` beside the installer, and the in-place update
inside Musa CAD (Windows, the Update dialog's **Download and Install**) refuses a download
whose SHA-256 differs from it. An installer attached by hand needs the same file beside it:

```sh
sha256sum MusaCAD-<version>-x86_64-setup.exe > MusaCAD-<version>-x86_64-setup.exe.sha256
gh release upload v<version> MusaCAD-<version>-x86_64-setup.exe MusaCAD-<version>-x86_64-setup.exe.sha256
```

The updater also requires the setup's version resource to say `<version>` (the NSIS script
stamps it from `/DVERSION`), so a setup built with a different version number than the tag
is refused on the user's machine. Without the `.sha256` the updater still checks the size
the release lists and the version resource, and says so in its dialog.

## Post-release

- Flathub: nothing by hand once the app is on Flathub. The tag push runs
  `.github/workflows/flathub-release.yml`, which opens the update pull request in
  `flathub/org.musacad.MusaCAD` (needs the `FLATHUB_TOKEN` secret); Flathub's own checker opens
  the same pull request from the manifest's `x-checker-data` if the workflow cannot. Merge it
  when its build is green (`packaging/flatpak/BUILD_FLATPAK.md`, "A release on Flathub").
- File follow-up issues for anything deferred or surfaced during verification.

## Notes / gotchas

- Flatpak uses the project's CMake `install()` rules. The AppImage script assembles
  its AppDir and bundles Qt with linuxdeploy.
- All app assets (ribbon icons, branding, hatch stock patterns) are compiled into the binary as Qt
  resources — there are no loose data files to ship.
- The headless CI/offscreen environment cannot exercise the GPU viewport or a real X session;
  verify the GUI on a real desktop. Automated checks use `MUSACAD_PLOT_TEST` (load + vector PDF)
  and `QT_QPA_PLATFORM=offscreen`.


## Native Wayland

The AppImage bundles the Wayland platform plugins (`libqwayland-generic.so`,
`libqwayland-egl.so`) and their shell/graphics/decoration integrations alongside xcb, so
on a Wayland session Qt's default platform order picks Wayland and the app runs natively
rather than through XWayland. That is a whole display refresh of pointer latency per frame.
Verify from the extracted AppImage exactly as for the headless paths:

```
QT_QPA_PLATFORM=wayland MUSACAD_SELFTEST=1 ./squashfs-root/AppRun   # expect: overall: PASS
```

`[musacad_ui] GL renderer: …` on stderr says which GPU/driver is drawing; on a Wayland
session the platform line Qt prints must not mention xcb.
