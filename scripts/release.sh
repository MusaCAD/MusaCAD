#!/usr/bin/env bash
# Musa CAD release helper. Builds the local Linux artifacts (AppImage + Flatpak) and prints
# the EXACT tag + publish commands for you to run. It never tags, pushes, or publishes --
# that stays a manual step (per project policy).
#
# Usage:
#   scripts/release.sh <version> [--dry-run]
#     <version>    e.g. 0.1.0  (no leading 'v')
#     --dry-run    build + verify artifacts only; still prints the publish commands
#
# The Windows installer is produced by GitHub Actions (.github/workflows/build-windows.yml),
# not here -- download it from the workflow run and add it to the `gh release create` line.
set -euo pipefail

VERSION="${1:?usage: scripts/release.sh <version> [--dry-run]}"
DRY_RUN=0
[[ "${2:-}" == "--dry-run" ]] && DRY_RUN=1

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"
TAG="v${VERSION}"
APPIMAGE="MusaCAD-${VERSION}-x86_64.AppImage"
FLATPAK="packaging/flatpak/MusaCAD-${VERSION}.flatpak"
WIN_SETUP="MusaCAD-${VERSION}-x86_64-setup.exe"
NOTES="docs/release-notes/v${VERSION}.md"

echo "================ Musa CAD release $TAG ================"

echo "==> [1/3] Building Linux AppImage"
packaging/linux/build_appimage.sh "$VERSION"

echo "==> [2/3] Building Flatpak bundle"
packaging/flatpak/build_flatpak.sh "$VERSION"

echo "==> [3/3] Verifying the AppImage (load a drawing + plot a vector PDF)"
SAMPLE="$REPO_ROOT/dwg_samples/single_block.musa"
if [[ -f "$SAMPLE" ]]; then
  tmp="$(mktemp -d)"; cp "$APPIMAGE" "$tmp/"
  ( cd "$tmp"
    APPIMAGE_EXTRACT_AND_RUN=1 QT_QPA_PLATFORM=offscreen \
      MUSACAD_PLOT_TEST="$SAMPLE|$tmp/verify.pdf|1" "./$APPIMAGE" >/dev/null 2>&1
    if [[ -s "$tmp/verify.pdf" ]]; then echo "    OK: produced $(du -h "$tmp/verify.pdf" | cut -f1) PDF"
    else echo "    WARNING: verify PDF not produced"; fi )
  rm -rf "$tmp"
else
  echo "    (no sample drawing at $SAMPLE -- skipped automated verify)"
fi

echo
echo "================ Artifacts ================"
ls -lh "$REPO_ROOT/$APPIMAGE" "$REPO_ROOT/$FLATPAK" 2>/dev/null || true
echo
if [[ $DRY_RUN -eq 1 ]]; then
  echo "DRY RUN: built + verified only. Nothing tagged or published."
fi
echo "================ Next steps (run these yourself) ================"
cat <<EOF
# 0. Ensure $NOTES exists and the Windows installer ($WIN_SETUP) has been
#    downloaded from the build-windows workflow run into the repo root.

# 1. Tag the release commit and push the tag:
git tag -a $TAG -m "Musa CAD $TAG"
git push origin $TAG

# 2. Create the GitHub release with all three artifacts attached:
gh release create $TAG \\
  --title "Musa CAD $TAG" \\
  --notes-file $NOTES \\
  "$APPIMAGE" \\
  "$FLATPAK" \\
  "$WIN_SETUP"
EOF
