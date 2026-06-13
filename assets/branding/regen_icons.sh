#!/usr/bin/env bash
# Regenerate all Musa CAD branding rasters from the single source SVG.
# Source of truth: assets/branding/musacad_logo.svg
# Outputs (committed): icons/hicolor/<N>x<N>/apps/musacad.png + musacad.ico
#
# SVG->PNG uses the Qt-based musacad_svg2png tool (ImageMagick cannot rasterise this SVG).
# PNG->ICO uses ImageMagick `convert` (PNG container assembly works without an SVG delegate).
set -euo pipefail
cd "$(dirname "$0")/../.."                 # repo root
SVG=assets/branding/musacad_logo.svg
OUT=assets/branding/icons/hicolor
TOOL=build/dev/bin/musacad_svg2png
SIZES=(16 24 32 48 64 128 256 512)

cmake --build --preset dev --target musacad_svg2png >/dev/null
for s in "${SIZES[@]}"; do
  dir="$OUT/${s}x${s}/apps"; mkdir -p "$dir"
  QT_QPA_PLATFORM=offscreen "$TOOL" "$SVG" "$dir" "musacad__tmp_" "$s" >/dev/null
  mv "$dir/musacad__tmp_${s}.png" "$dir/musacad.png"
done

# Windows multi-resolution icon from the PNGs.
if command -v convert >/dev/null 2>&1; then
  convert "$OUT/16x16/apps/musacad.png" "$OUT/32x32/apps/musacad.png" \
          "$OUT/48x48/apps/musacad.png" "$OUT/64x64/apps/musacad.png" \
          "$OUT/128x128/apps/musacad.png" "$OUT/256x256/apps/musacad.png" \
          assets/branding/musacad.ico
  echo "regen: wrote assets/branding/musacad.ico"
else
  echo "regen: WARNING convert not found -- skipped musacad.ico (PNGs still regenerated)"
fi
echo "regen: hicolor PNGs + ico regenerated from $SVG"
