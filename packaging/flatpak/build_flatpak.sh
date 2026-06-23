#!/usr/bin/env bash
# Build a Musa CAD Flatpak bundle locally and (optionally) install it.
# Produces packaging/flatpak/MusaCAD-<version>.flatpak, installable with:
#     flatpak install --user packaging/flatpak/MusaCAD-<version>.flatpak
#     flatpak run com.musacad.MusaCAD
#
# Prereqs (user-level, no root):
#     flatpak install --user -y flathub org.kde.Platform//6.10 org.kde.Sdk//6.10 org.flatpak.Builder
#
# Usage:  packaging/flatpak/build_flatpak.sh [VERSION]   (default VERSION=0.1.0)
set -euo pipefail

VERSION="${1:-0.1.0}"
APPID="com.musacad.MusaCAD"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
SRC="$HERE/.src"                 # clean source tree the manifest points at (gitignored)
BUILDDIR="$HERE/build"           # flatpak-builder output (gitignored)
REPODIR="$HERE/.repo"            # local ostree repo (gitignored)
BUNDLE="$HERE/MusaCAD-${VERSION}.flatpak"

# flatpak-builder is shipped as a flatpak (org.flatpak.Builder); fall back to a native one.
if command -v flatpak-builder >/dev/null 2>&1; then
  FB=(flatpak-builder)
else
  FB=(flatpak run org.flatpak.Builder)
fi

echo "==> Staging a clean source tree into $SRC"
rm -rf "$SRC"; mkdir -p "$SRC"
# Copy only what the build needs; exclude build outputs, VCS, caches, large samples.
rsync -a --delete \
  --exclude '.git/' --exclude 'build/' --exclude 'plot_out/' --exclude 'dwg_samples/' \
  --exclude 'packaging/linux/.tools/' --exclude 'packaging/flatpak/.src/' \
  --exclude 'packaging/flatpak/build/' --exclude 'packaging/flatpak/.repo/' \
  --exclude '*.AppImage' --exclude '*.flatpak' \
  "$REPO_ROOT/" "$SRC/"

echo "==> Building the Flatpak ($APPID, branch stable)"
rm -rf "$BUILDDIR" "$REPODIR"
"${FB[@]}" --user --force-clean --default-branch=stable --repo="$REPODIR" "$BUILDDIR" "$HERE/$APPID.yml"

echo "==> Exporting a single-file bundle: $BUNDLE"
# 4th arg is the BRANCH (not the version) -- must match --default-branch above.
flatpak build-bundle "$REPODIR" "$BUNDLE" "$APPID" stable

echo "==> Built: $BUNDLE"
ls -lh "$BUNDLE"
echo
echo "Install + run:"
echo "    flatpak install --user -y $BUNDLE"
echo "    flatpak run $APPID"
