#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Pranay Kiran
#
# Prepare the FIRST Flathub submission of Musa CAD (issue #1) -- everything up to the pull
# request itself, which Flathub requires a person to open and to answer for (its checklist
# includes affirmations only the submitter can make). Run it yourself:
#
#     packaging/flatpak/flathub/submit.sh
#
# It forks github.com/flathub/flathub under your GitHub account (if not already), clones the
# fork's `new-pr` branch, adds org.musacad.MusaCAD.yml at the top level, commits, pushes a
# branch, and prints the `gh pr create` command with PR_BODY_DRAFT.md as the body: read the
# draft, edit it in your own words (the AI-disclosure line included), tick what is true,
# then run the printed command.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ID="org.musacad.MusaCAD"
USER="$(gh api user -q .login)"
WORK="$(mktemp -d)"

echo "==> Forking flathub/flathub under ${USER} (no-op if it exists)"
gh repo fork flathub/flathub --clone=false >/dev/null 2>&1 || true

echo "==> Cloning the fork's new-pr branch"
git clone --quiet --branch new-pr --single-branch "https://github.com/${USER}/flathub.git" "$WORK/flathub"
cd "$WORK/flathub"
git checkout -q -b "$APP_ID"
cp "$HERE/$APP_ID.yml" "./$APP_ID.yml"
git add "$APP_ID.yml"
git commit -q -m "Add $APP_ID"
git push -q -u origin "$APP_ID"

echo
echo "==> Branch pushed: ${USER}/flathub:${APP_ID}"
echo "    Now read and edit the draft, then open the pull request yourself:"
echo
echo "    \$EDITOR $HERE/PR_BODY_DRAFT.md"
echo "    gh pr create --repo flathub/flathub --base new-pr --head ${USER}:${APP_ID} \\"
echo "      --title 'Add $APP_ID' --body-file $HERE/PR_BODY_DRAFT.md"
echo
echo "    The linter will report finish-args-home-filesystem-access; the draft states why"
echo "    the exception is needed. Work tree: $WORK/flathub"
