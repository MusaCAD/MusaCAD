#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
#
# Assemble MusaCAD.app from a built musacad_app binary and run macdeployqt on it.
#
#     packaging/macos/build_app.sh VERSION path/to/musacad_app
#
# Produces ./MusaCAD.app (Qt frameworks and plugins bundled, the offscreen platform
# plugin included so `--check` / `--plot` work headless). Needs Qt 6's macdeployqt on
# PATH (or QT_ROOT_DIR / Qt6_DIR pointing at the Qt kit), and rsvg-convert for the icon
# (brew install librsvg); without rsvg-convert the bundle simply has no icon.
set -euo pipefail

VERSION="${1:?VERSION}"
BIN="${2:?path to musacad_app}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
APP="MusaCAD.app"

# Qt tools: macdeployqt next to qmake, wherever the kit is.
QT_BIN=""
for cand in "${QT_ROOT_DIR:-}/bin" "${Qt6_DIR:-}/../../../bin" "$(dirname "$(command -v qmake 2>/dev/null || true)")"; do
  if [ -n "$cand" ] && [ -x "$cand/macdeployqt" ]; then
    QT_BIN="$cand"
    break
  fi
done
[ -n "$QT_BIN" ] || { echo "macdeployqt not found (set QT_ROOT_DIR)"; exit 1; }
QT_PLUGINS="$("$QT_BIN/qmake" -query QT_INSTALL_PLUGINS)"

echo "==> Assembling $APP ($VERSION)"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/musacad_app"
chmod 755 "$APP/Contents/MacOS/musacad_app"
sed "s/@VERSION@/$VERSION/g" "$HERE/Info.plist.in" > "$APP/Contents/Info.plist"
printf 'APPL????' > "$APP/Contents/PkgInfo"

# Icon: the SVG logo rendered at the sizes iconutil wants.
if command -v rsvg-convert >/dev/null 2>&1; then
  ICONSET="$(mktemp -d)/MusaCAD.iconset"
  mkdir -p "$ICONSET"
  for size in 16 32 128 256 512; do
    rsvg-convert -w "$size" -h "$size" "$REPO_ROOT/assets/branding/musacad_logo.svg" \
      -o "$ICONSET/icon_${size}x${size}.png"
    rsvg-convert -w "$((size * 2))" -h "$((size * 2))" "$REPO_ROOT/assets/branding/musacad_logo.svg" \
      -o "$ICONSET/icon_${size}x${size}@2x.png"
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/MusaCAD.icns"
else
  echo "WARNING: rsvg-convert not found; the bundle has no icon"
fi

echo "==> macdeployqt"
"$QT_BIN/macdeployqt" "$APP" -verbose=1
# `--plot` runs on Qt's offscreen platform; macdeployqt bundles only cocoa by itself. The
# plugin's Qt dependencies are already in the bundle (it links the same QtGui / QtCore
# through @rpath, resolved against the executable's Frameworks rpath).
install -m 755 "$QT_PLUGINS/platforms/libqoffscreen.dylib" "$APP/Contents/PlugIns/platforms/libqoffscreen.dylib"

echo "==> Built: $APP"
ls -la "$APP/Contents/MacOS" "$APP/Contents/PlugIns/platforms"
