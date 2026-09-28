#!/bin/sh
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
#
# Regenerate assets/screenshots/*.png -- the pictures in the README and on Flathub.
#
#   tools/screenshots/capture.sh [path/to/musacad_app]
#
# The drawings come from make_drawings.py. Each picture is made by the app itself
# (MUSACAD_LISTING_SHOT): the window's own widgets and the viewport's own OpenGL frame,
# composed in the app -- never a screen grab, so nothing the desktop draws over the window
# (a volume or brightness OSD, a notification, the pointer) can get into it. The window is
# 1000 x 700, the size Flathub's guidelines ask for.
#
# Needs a desktop session with OpenGL 4.5 (a window opens for a few seconds per picture)
# and a release build (the default is build/release/bin/musacad_app).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
app=${1:-$root/build/release/bin/musacad_app}
out=$root/assets/screenshots

if [ ! -x "$app" ]; then
    echo "capture.sh: no app at $app (build the release preset, or pass its path)" >&2
    exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM
python3 "$here/make_drawings.py" "$work"
mkdir -p "$out"

for shot in 0:overview 1:architectural-plan 2:mechanical-detail 3:hatch-section \
    4:command-entry 5:dynamic-input; do
    kind=${shot%%:*}
    name=${shot#*:}
    MUSACAD_LISTING_SHOT="$kind|$work|$out/$name.png" "$app" | grep '^\[listing_shot\]' || {
        echo "capture.sh: $name failed" >&2
        exit 1
    }
done
