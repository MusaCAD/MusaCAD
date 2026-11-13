#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
# Sign nested code inside-out after all deployment changes. '-' is ad-hoc signing,
# which validates the bundle's integrity but does NOT establish Gatekeeper trust.
set -euo pipefail
APP="${1:?path to .app}"
IDENTITY="${2:--}"
OPTIONS=(--force --sign "$IDENTITY")
if [ "$IDENTITY" != "-" ]; then
  OPTIONS+=(--options runtime --timestamp)
fi

# Do not follow framework symlinks and sign the same object more than once.
while IFS= read -r -d '' item; do
  if file -b "$item" | grep -q 'Mach-O'; then
    codesign "${OPTIONS[@]}" "$item"
  fi
done < <(find "$APP/Contents" -type f -print0)
# Frameworks/helpers are sealed before their enclosing bundle, never with --deep signing.
while IFS= read -r -d '' item; do
  codesign "${OPTIONS[@]}" "$item"
done < <(find "$APP/Contents" -depth -type d \( -name '*.framework' -o -name '*.app' -o -name '*.xpc' -o -name '*.bundle' \) -print0)
codesign "${OPTIONS[@]}" "$APP"
codesign --verify --deep --strict --verbose=2 "$APP"
