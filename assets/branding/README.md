# Musa CAD — branding assets

**Single source of truth:** [`musacad_logo.svg`](musacad_logo.svg). Everything else is
derived from it — never hand-edit the rasters.

| Artifact | Purpose | How it's used |
|---|---|---|
| `musacad_logo.svg` | the logo (vector) | embedded via `branding.qrc` as `:/branding/musacad_logo.svg` |
| `icons/hicolor/<N>x<N>/apps/musacad.png` | Linux launcher / menu icons | install to `…/share/icons/hicolor/` |
| `musacad.ico` | Windows executable icon (multi-res) | linked via `musacad.rc` (built only `if(WIN32)`) |
| `musacad.desktop` | Linux app/launcher entry | install to `…/share/applications/` |
| `branding.qrc` | Qt resource embedding the SVG | `AUTORCC` on `musacad_app` |

## In-app use
- **Window / taskbar icon:** `QApplication::setWindowIcon(QIcon(":/branding/musacad_logo.svg"))`
  in `src/app/main.cpp` — Qt rasterises the SVG at whatever size the desktop requests.
- **QAT application button:** the logo mark (replaces the old "M"); opens **About**.
- **Help → About:** logo + name + version + build stamp + LGPL (`MainWindow::show_about`).

## Regenerating the rasters
```sh
./assets/branding/regen_icons.sh
```
Renders the SVG → hicolor PNGs with the Qt-based `musacad_svg2png` tool (ImageMagick cannot
rasterise this SVG), then assembles `musacad.ico` from the PNGs with ImageMagick `convert`.
Requires a configured `dev` build preset and `convert` (for the `.ico` only).
