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
- Install + launch the Flatpak (`flatpak install --user ...flatpak`; `flatpak run com.musacad.MusaCAD`).
- **Windows: a human installs the `.exe` on a real Windows box** and confirms it launches + draws.
  Claude Code / Linux CI cannot verify the Windows binary at runtime — this step is manual.

## Tag + publish (manual — never automated)

```sh
git tag -a v<version> -m "Musa CAD v<version>"
git push origin v<version>

gh release create v<version> \
  --title "Musa CAD v<version>" \
  --notes-file docs/release-notes/v<version>.md \
  MusaCAD-<version>-x86_64.AppImage \
  packaging/flatpak/MusaCAD-<version>.flatpak \
  MusaCAD-<version>-x86_64-setup.exe
```

Then confirm on GitHub: the release page shows all three artifacts, notes render correctly,
and the `v<version>` tag is visible.

## Post-release

- Flathub submission (manifest is ready; see `packaging/flatpak/BUILD_FLATPAK.md` → "Flathub — STAGED").
- File follow-up issues for anything deferred or surfaced during verification.

## Notes / gotchas

- The project has no CMake `install()` rules; both the AppImage and Flatpak assemble `/app`-style
  trees by hand (binary + `.desktop` + SVG icon + AppStream metainfo). If install rules are added
  later, simplify the packaging scripts accordingly.
- All app assets (ribbon icons, branding, hatch stock patterns) are compiled into the binary as Qt
  resources — there are no loose data files to ship.
- The headless CI/offscreen environment cannot exercise the GPU viewport or a real X session;
  verify the GUI on a real desktop. Automated checks use `MUSACAD_PLOT_TEST` (load + vector PDF)
  and `QT_QPA_PLATFORM=offscreen`.
