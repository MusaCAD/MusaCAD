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
# It forks github.com/flathub/flathub under your GitHub account (if not already), clones
# Flathub's `new-pr` branch over SSH, adds org.musacad.MusaCAD.yml at the top level, commits,
# pushes a branch to your fork over SSH, and prints the `gh pr create` command with
# PR_BODY_DRAFT.md as the body: read the
# draft, edit it in your own words (the AI-disclosure line included), tick what is true,
# then run the printed command.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ID="org.musacad.MusaCAD"
USER="$(gh api user -q .login)"
WORK="$(mktemp -d)"

# SSH throughout (your key, no credential prompts): the upstream new-pr branch is read,
# your fork is written.
UPSTREAM="git@github.com:flathub/flathub.git"
FORK="git@github.com:${USER}/flathub.git"

echo "==> Forking flathub/flathub under ${USER} (no-op if it exists)"
if ! gh api "repos/${USER}/flathub" >/dev/null 2>&1; then
  gh repo fork flathub/flathub --clone=false
  for i in $(seq 1 30); do # a fresh fork takes a moment to exist
    gh api "repos/${USER}/flathub" >/dev/null 2>&1 && break
    sleep 2
  done
fi
gh api "repos/${USER}/flathub" >/dev/null 2>&1 || { echo "the fork ${USER}/flathub is not there yet; run again in a minute"; exit 1; }

echo "==> Cloning flathub/flathub's new-pr branch (the submission base)"
git clone --quiet --depth 1 --branch new-pr --single-branch "$UPSTREAM" "$WORK/flathub"
cd "$WORK/flathub"
git checkout -q -b "$APP_ID"
cp "$HERE/$APP_ID.yml" "./$APP_ID.yml"
git add "$APP_ID.yml"
git commit -q -m "Add $APP_ID"
echo "==> Pushing ${USER}/flathub:${APP_ID}"
git push -q --force "$FORK" "$APP_ID"

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
