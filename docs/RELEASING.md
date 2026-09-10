<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Releasing Musa CAD

The playbook for cutting a release (v0.2.0 and onward). v0.1.0 followed this exact flow.

## What gets produced

| Platform | Artifact | Built by |
|---|---|---|
| Linux | `MusaCAD-<ver>-x86_64.AppImage` | `packaging/linux/build_appimage.sh` (local) **and** `.github/workflows/build-linux.yml` (CI) |
| Linux | `MusaCAD-<ver>.flatpak` | `packaging/flatpak/build_flatpak.sh` (local) |
| Windows | `MusaCAD-<ver>-x86_64-setup.exe` | `.github/workflows/build-windows.yml` (CI, `windows-latest`) |

Packaging detail lives in `packaging/linux/BUILD_APPIMAGE.md` and `packaging/flatpak/BUILD_FLATPAK.md`.

## Versioning

- `project(MusaCAD VERSION X.Y.Z ...)` in the top `CMakeLists.txt` is the source of truth.
- Tags are `vX.Y.Z`. Pushing a `v*` tag triggers **both** CI workflows automatically, so the
  Linux AppImage + Windows installer are produced for every tagged release.

## Pre-flight (do not skip)

1. `cmake --preset dev && ctest --preset dev` — all tests green.
2. `cmake --build --preset release` — clean, no warnings.
3. Bump `project(... VERSION ...)` if needed; update `CHANGELOG.md` and write the tracked
   release notes at `docs/release-notes/v<ver>.md`.
4. Re-run the license scan (`docs/THIRD_PARTY_LICENSES.md` → "Reproducing the scan"): no GPL/DWG
   library in the build graph, the binary, or the bundles; the DWG converter stays external.

## Cut the release

```sh
# Build + verify the Linux artifacts and print the publish commands:
scripts/release.sh <version>          # add --dry-run to build/verify without printing 'go' intent

# Trigger the Windows installer build (or just push the tag, which triggers both workflows):
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
- Install + launch the Flatpak (`flatpak install --user ...flatpak`; `flatpak run com.musacad.MusaCAD`).
- **Windows: a human installs the `.exe` on a real Windows box** and confirms it launches + draws.
  Claude Code / Linux CI cannot verify the Windows binary at runtime — this step is manual.
  Check both programs the installer ships: `musacad_app.exe` from the Start menu (no console
  window), and `musacad.exe --check <drawing>` / `--plot` from cmd or PowerShell (waits and
  returns the exit code). Open a drawing from a folder with a non-ASCII name too.

## Tag + publish

Pushing the tag is the release action. The Linux and Windows workflows build on the tag
and their `publish` jobs create the GitHub release for it (whichever finishes first) and
attach the AppImage and the Windows installer.

```sh
git tag -a v<version> -m "Musa CAD v<version>"
git push origin v<version>
```

Then, once both workflows are green, set the notes and attach the Flatpak (built locally):

```sh
gh release edit v<version> \
  --title "Musa CAD v<version>" \
  --notes-file docs/release-notes/v<version>.md
gh release upload v<version> packaging/flatpak/MusaCAD-<version>.flatpak
```

Confirm on GitHub: the release page shows all three artifacts, the notes render correctly,
and the `v<version>` tag is visible. A tag pushed before the Windows build has been verified
on real hardware still publishes both installers, so verify first (see above) or delete the
Windows asset from the release afterwards.

## Post-release

- Flathub submission (manifest is ready; see `packaging/flatpak/BUILD_FLATPAK.md` → "Flathub — STAGED").
- File follow-up issues for anything deferred or surfaced during verification.

## Linux-only releases

A release may ship **Linux only** when the Windows installer has not yet been verified on
real hardware (issue #6). In that case: build and publish the AppImage, say so plainly at
the top of the release notes, and leave the Windows artifact for the person with the
Windows box to add to the same GitHub release afterwards. Do not publish an unverified
`.exe` just because CI produced one.

## Notes / gotchas

- The project has no CMake `install()` rules; both the AppImage and Flatpak assemble `/app`-style
  trees by hand (binary + `.desktop` + SVG icon + AppStream metainfo). If install rules are added
  later, simplify the packaging scripts accordingly.
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
