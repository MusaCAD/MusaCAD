#!/usr/bin/env bash
# Build a standalone Musa CAD AppImage that bundles musacad_app + all Qt6 runtime
# dependencies (incl. the qsvg image plugin, required for the ribbon/branding SVGs) +
# the xcb + offscreen platform plugins. Produces MusaCAD-<version>-x86_64.AppImage in
# the repo root.
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

# 3. Assemble a clean AppDir by hand: the binary, desktop entry and icon only (`cmake
#    --install` would also place the AppStream metadata, which the AppImage does not carry).
echo "==> Assembling AppDir"
rm -rf "$APPDIR"
install -Dm755 "$BIN"                                   "$APPDIR/usr/bin/musacad_app"
install -Dm644 "$REPO_ROOT/assets/branding/org.musacad.MusaCAD.desktop" \
        "$APPDIR/usr/share/applications/org.musacad.MusaCAD.desktop"
# SVG (scalable) icon -- named to match the .desktop's `Icon=org.musacad.MusaCAD`. No PNG
# raster is needed (AppImage supports scalable icons; the app itself renders SVG via qsvg).
install -Dm644 "$REPO_ROOT/assets/branding/musacad_logo.svg" \
        "$APPDIR/usr/share/icons/hicolor/scalable/apps/org.musacad.MusaCAD.svg"

# 4. Run linuxdeploy with the Qt plugin. EXTRA_PLATFORM_PLUGINS pins the platform plugins
#    we actually need; the qt plugin auto-bundles imageformats (qsvg/qico), iconengines,
#    platforms, styles.
#
#    `offscreen` is REQUIRED, not optional: `musacad --plot` (issue #11) forces the
#    offscreen platform so a headless plot works from cron/CI/ssh. Without it bundled the
#    AppImage aborts with "Could not find the Qt platform plugin \"offscreen\"" -- which is
#    how this was found, by running --plot from an extracted AppImage rather than trusting
#    that the desktop path implied the headless one. `minimal` costs nothing and is the
#    documented fallback for a Qt app with no windowing system.
echo "==> Running linuxdeploy + qt plugin"
export QMAKE
#    `wayland-generic` + `wayland-egl` (with the shell/graphics/decoration integration
#    plugins below) let the app run NATIVELY on a Wayland session instead of through
#    XWayland. That removes one compositor hop -- a whole display refresh of pointer
#    latency -- from every frame. Qt's default platform order ("wayland;xcb") picks it
#    automatically when the session is Wayland; xcb remains for X11 sessions.
export EXTRA_PLATFORM_PLUGINS="libqxcb.so;libqwayland-generic.so;libqwayland-egl.so;libqoffscreen.so;libqminimal.so"
export EXTRA_QT_PLUGINS="svg;imageformats/qsvg;iconengines/qsvgicon;styles;wayland-shell-integration;wayland-graphics-integration-client;wayland-decoration-client"
export OUTPUT="$OUT"
rm -f "$REPO_ROOT/$OUT"
# Pass 1: deploy the app, Qt, and the plugins the qt plugin knows how to copy.
"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
  --appdir "$APPDIR" \
  --executable "$APPDIR/usr/bin/musacad_app" \
  --desktop-file "$APPDIR/usr/share/applications/org.musacad.MusaCAD.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/org.musacad.MusaCAD.svg" \
  --plugin qt

# The Wayland CLIENT BUFFER integration is what lets a GL context exist under native
# Wayland, and linuxdeploy-plugin-qt does not bundle that directory (it copies the
# shell and decoration ones). Without it Qt logs `Failed to load client buffer
# integration: "wayland-egl"`, the GL viewport cannot be created, and the app is blank
# -- found by running the GUI self-test from the built AppImage under
# QT_QPA_PLATFORM=wayland, which is now part of the release check. Copy it in from the
# host Qt; its own libraries (WaylandEglClientHwIntegration, wayland-egl) were already
# deployed by pass 1, and libEGL is a host graphics library that must stay unbundled.
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS)"
CLIENT_INTEGRATION="$QT_PLUGINS/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so"
if [[ -f "$CLIENT_INTEGRATION" ]]; then
  install -Dm755 "$CLIENT_INTEGRATION" \
          "$APPDIR/usr/plugins/wayland-graphics-integration-client/libqt-plugin-wayland-egl.so"
else
  echo "WARNING: $CLIENT_INTEGRATION not found; the AppImage will fall back to XWayland"
fi

# Pass 2: deploy the dependencies of everything now in the AppDir and write the image.
"$TOOLS_DIR/linuxdeploy-${ARCH}.AppImage" \
  --appdir "$APPDIR" \
  --output appimage

[[ -f "$REPO_ROOT/$OUT" ]] || { echo "ERROR: $OUT was not produced"; exit 1; }
echo "==> Built: $REPO_ROOT/$OUT"
ls -lh "$REPO_ROOT/$OUT"
