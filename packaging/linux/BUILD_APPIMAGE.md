# Building the Musa CAD AppImage

A single self-contained `MusaCAD-<version>-x86_64.AppImage` that bundles `musacad_app`
plus all Qt 6 runtime libraries and the plugins it needs at runtime (the **qsvg** image
plugin + **qsvgicon** icon engine — required for the ribbon/branding SVGs — and the **xcb**
platform plugin). It runs on any reasonably recent glibc Linux without installing Qt.

## One command

```sh
packaging/linux/build_appimage.sh 0.1.0
```

This is the same script CI runs (`.github/workflows/build-linux.yml`). It:

1. Configures + builds the **release** preset (`cmake --preset release && cmake --build --preset release --target musacad_app`). Set `BUILD=0` to skip if already built.
2. Downloads the deploy tools on demand into `packaging/linux/.tools/` (gitignored):
   `linuxdeploy`, `linuxdeploy-plugin-qt`, `appimagetool` (the `continuous` releases).
3. Assembles `build/AppDir/` by hand (the project has no `install()` rules):
   - `usr/bin/musacad_app`
   - `usr/share/applications/musacad.desktop` (from `assets/branding/`)
   - `usr/share/icons/hicolor/scalable/apps/musacad.svg` (the logo, named to match the
     desktop file's `Icon=musacad`; AppImage supports scalable icons, so no PNG raster is needed)
4. Runs `linuxdeploy --plugin qt`, which pulls in Qt's libraries + plugins. `EXTRA_QT_PLUGINS`
   pins `svg;imageformats/qsvg;iconengines/qsvgicon;styles`; `EXTRA_PLATFORM_PLUGINS=libqxcb.so`.
   `QMAKE` defaults to `qmake6` (the qt plugin uses it to locate Qt).
5. Emits `MusaCAD-<version>-x86_64.AppImage` in the repo root (~32 MB).

`APPIMAGE_EXTRACT_AND_RUN=1` is exported so the tool-AppImages work without FUSE (CI runners
have no FUSE). The produced AppImage itself runs via FUSE *or* the same extract-and-run fallback.

## Prerequisites

- A working `cmake --preset release` build (system Qt 6, a C++23 compiler).
- `qmake6` on `PATH` (Debian/Ubuntu: `qmake6` from `qt6-base-dev`).
- `curl`, and network access on first run (to fetch the deploy tools).

## What is NOT bundled (by design)

- **LibreDWG / ODA File Converter** — DWG import/export shells out to an external converter
  discovered on `PATH` at runtime (`QStandardPaths::findExecutable`). It is never linked or
  shipped, keeping the AppImage LGPL-clean. DWG support is opt-in: the user installs the
  converter separately.
- Application assets (ribbon icons, branding, hatch pattern stock library) are **compiled into
  the binary** as Qt resources (`branding.qrc`, `ribbon.qrc`), so there are no loose data files.

## Verifying the AppImage

Run it from a directory the build never touched (e.g. `/tmp`):

```sh
cp MusaCAD-0.1.0-x86_64.AppImage /tmp && cd /tmp

# Launches + GUI renders (ribbon SVG icons prove the bundled qsvg plugin loads):
./MusaCAD-0.1.0-x86_64.AppImage

# Headless load-a-drawing + plot-a-PDF self-test (exits 0 on success):
MUSACAD_PLOT_TEST="/path/to/drawing.musa|/tmp/out.pdf|1" ./MusaCAD-0.1.0-x86_64.AppImage
#   -> /tmp/out.pdf is a vector PDF (verify: `pdfimages -list out.pdf` lists no raster images)
```

Last verified on this repo: launches, ribbon + SVG icons render, loads
`dwg_samples/single_block.musa` (27,246 lines), plots a 121 KB vector A4 PDF (Producer Qt 6.4.2,
0 raster images), exits cleanly.
