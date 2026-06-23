#!/usr/bin/env bash
# Build a standalone Musa CAD AppImage that bundles musacad_app + all Qt6 runtime
# dependencies (incl. the qsvg image plugin, required for the ribbon/branding SVGs) +
# the xcb platform plugin. Produces MusaCAD-<version>-x86_64.AppImage in the repo root.
#
# Reused verbatim by .github/workflows/build-linux.yml, so it must be self-contained:
# it downloads linuxdeploy + linuxdeploy-plugin-qt + appimagetool on demand.
#
# Usage:  packaging/linux/build_appimage.sh [VERSION]   (default VERSION=0.1.0)
# Env:    QMAKE   path to qmake6 (default: autodetected)
#         BUILD   1 = (re)configure + build the release preset first (default: 1)
set -euo pipefail

VERSION="${1:-0.1.0}"
ARCH="x86_64"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$REPO_ROOT"

TOOLS_DIR="${TOOLS_DIR:-$REPO_ROOT/packaging/linux/.tools}"   # gitignored cache
APPDIR="$REPO_ROOT/build/AppDir"
OUT="MusaCAD-${VERSION}-${ARCH}.AppImage"
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export APPIMAGE_EXTRACT_AND_RUN=1   # run the tool-AppImages without FUSE

echo "==> Musa CAD AppImage build  (version=$VERSION, qmake=$QMAKE)"

# 1. Build the release binary (preset 'release') unless BUILD=0.
if [[ "${BUILD:-1}" == "1" ]]; then
  echo "==> Configuring + building release preset"
  cmake --preset release >/dev/null
  cmake --build --preset release --target musacad_app
fi
BIN="$REPO_ROOT/build/release/bin/musacad_app"
[[ -x "$BIN" ]] || { echo "ERROR: $BIN not found/executable"; exit 1; }

# 2. Fetch the deploy tools (idempotent).
mkdir -p "$TOOLS_DIR"
fetch() { # url dest
  local url="$1" dest="$2"
  if [[ ! -x "$dest" ]]; then
    echo "==> Downloading $(basename "$dest")"
    curl -fsSL "$url" -o "$dest"
    chmod +x "$dest"
  fi
}
fetch "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
      "$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage"
fetch "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
      "$TOOLS_DIR/linuxdeploy-plugin-qt-${ARCH}.AppImage"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
      "$TOOLS_DIR/appimagetool-${ARCH}.AppImage"
export PATH="$TOOLS_DIR:$PATH"

# 3. Assemble a clean AppDir (no CMake install() rules, so do it by hand).
echo "==> Assembling AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BIN"                                   "$APPDIR/usr/bin/musacad_app"
install -Dm644 "$REPO_ROOT/assets/branding/musacad.desktop" \
        "$APPDIR/usr/share/applications/musacad.desktop"
# SVG (scalable) icon -- named to match the .desktop's `Icon=musacad`. No PNG raster
# is needed (AppImage supports scalable icons; the app itself renders SVG via qsvg).
install -Dm644 "$REPO_ROOT/assets/branding/musacad_logo.svg" \
        "$APPDIR/usr/share/icons/hicolor/scalable/apps/musacad.svg"

# 4. Run linuxdeploy with the Qt plugin. EXTRA_PLATFORM_PLUGINS pins xcb; the qt plugin
#    auto-bundles imageformats (qsvg/qico), iconengines, platforms, styles.
echo "==> Running linuxdeploy + qt plugin"
export QMAKE
export EXTRA_PLATFORM_PLUGINS="libqxcb.so"
export EXTRA_QT_PLUGINS="svg;imageformats/qsvg;iconengines/qsvgicon;styles"
export OUTPUT="$OUT"
rm -f "$REPO_ROOT/$OUT"
"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/musacad_app" \
  --desktop-file "$APPDIR/usr/share/applications/musacad.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/musacad.svg" \
  --plugin qt \
  --output appimage

[[ -f "$REPO_ROOT/$OUT" ]] || { echo "ERROR: $OUT was not produced"; exit 1; }
echo "==> Built: $REPO_ROOT/$OUT"
ls -lh "$REPO_ROOT/$OUT"
