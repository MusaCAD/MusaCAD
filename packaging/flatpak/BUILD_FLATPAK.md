# Building the Musa CAD Flatpak

Produces a single-file `MusaCAD-<version>.flatpak` bundle that users install with
`flatpak install --user MusaCAD-<version>.flatpak` and run with
`flatpak run org.musacad.MusaCAD`.

Built against the **KDE 6.11** runtime (`org.kde.Platform` // `org.kde.Sdk`), which provides
Qt 6. The app is compiled from source inside the sandbox (Flathub-style).

## Prerequisites (all user-level, no root)

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.kde.Platform//6.11 org.kde.Sdk//6.11 org.flatpak.Builder
```

`org.flatpak.Builder` provides `flatpak-builder` as a flatpak (no distro package / root needed).
You also need `rsync` on the host.

## One command

```sh
packaging/flatpak/build_flatpak.sh 0.1.0
```

The script:

1. Rsyncs a **clean** working tree into `packaging/flatpak/.src` (gitignored), excluding
   `build/`, `.git/`, samples, and prior artifacts — so uncommitted changes are built without
   copying the huge `build/` tree. The manifest's `dir` source points at `.src`.
2. Runs `flatpak-builder` against `org.musacad.MusaCAD.yml`: a `cmake-ninja` build of the
   **Release** configuration inside the sandbox (the developer tools in `tests/` off), then the
   project's `install()` rules place the binary, `org.musacad.MusaCAD.desktop`, the SVG icon
   (as `org.musacad.MusaCAD.svg`) and the AppStream `.metainfo.xml` under `/app`.
3. Exports a single-file bundle `packaging/flatpak/MusaCAD-<version>.flatpak`.

## Install + verify

```sh
flatpak install --user -y packaging/flatpak/MusaCAD-0.1.0.flatpak
flatpak run org.musacad.MusaCAD                       # launches the GUI

# Headless load + plot self-test (pass envs through the sandbox; the one-off
# --filesystem grant lets it read and write paths it was not handed by a dialog):
flatpak run --filesystem=home --env=MUSACAD_PLOT_TEST="$HOME/drawing.musa|$HOME/out.pdf|1" org.musacad.MusaCAD
```

## What is bundled / what is not

- The KDE runtime supplies Qt 6 + the platform/imageformats plugins (incl. **qsvg**); nothing
  extra is vendored. Musa CAD's own assets are compiled into the binary (Qt resources).
- **DWG import/export** shells out to an external converter (ODA File Converter / LibreDWG
  `dwg2dxf`); none is shipped, so the Flatpak stays LGPL-clean and DXF is what works out of
  the box. **"Download ODA File Converter"** in DWG Setup fetches the free converter from
  opendesign.com into the app's data directory (`--share=network` exists for that). The
  converter needs an X11 display: under an X11 session it runs inside the sandbox
  (`fallback-x11`); under Wayland the sandbox has no display, so it runs on the host
  through `flatpak-spawn --host`, which you allow once with
  `flatpak override --user --talk-name=org.freedesktop.Flatpak org.musacad.MusaCAD`. A
  converter installed on the host is used the same way (**"Use a converter installed on
  the host"**); discovery, the run and the converter's scratch files (under the app's cache
  directory, a path the host sees) all go through the portal then.
- **No filesystem access.** Every file dialog is the desktop's own, which Qt routes through the
  file-chooser portal: the app gets the file picked, nothing else. A drawing whose external
  references or images sit beside it offers to open from its folder instead (the portal's
  folder chooser grants it), so relative paths resolve as usual; a DXF export whose drawing
  embeds images asks for the destination folder the same way, since DXF writes those images
  as files beside it. Save As keeps a name typed without the extension as typed, because the
  portal grants exactly that name. The OpenGL viewport uses `--device=dri`; X11
  (`fallback-x11`) and Wayland sockets are granted.

## Flathub submission — PREPARED

`flathub/org.musacad.MusaCAD.yml` is the submission manifest: the same module as the local one,
with the tagged release as its source (`tag` + `commit`, updated together for each release).
The AppStream metainfo carries a `<release>` per version and its screenshot URLs resolve on
`main`. Lint both before a submission (the linter ships with `org.flatpak.Builder`; its sandbox
needs to see the files):

```sh
flatpak run --filesystem="$PWD" --command=flatpak-builder-lint org.flatpak.Builder \
  --exceptions --exceptions-repo stable manifest "$PWD/packaging/flatpak/flathub/org.musacad.MusaCAD.yml"
flatpak run --filesystem="$PWD" --command=flatpak-builder-lint org.flatpak.Builder \
  appstream "$PWD/packaging/flatpak/org.musacad.MusaCAD.metainfo.xml"
```

Both pass clean. The app id is `org.musacad.MusaCAD`, the reverse of the project's domain
(`https://musacad.org/`, the metainfo's homepage), which is what Flathub verifies. The
manifest asks for no filesystem permission (see above) and has no build commands of its own:
`cmake-ninja` plus the project's `install()` rules, with the desktop entry already named and
pointing at the app-id icon.

### The first submission (a person's pull request)

Flathub's reviewers refuse a submission whose text was generated by an AI assistant and
close for good one whose AI disclosure turns out to be untrue (they read the source
repository, which credits Claude in README.md and CONTRIBUTORS.md).
The pull request is therefore yours to write and to answer for. `flathub/submit.sh` does the
mechanics up to it -- it forks `github.com/flathub/flathub` under your account, clones
Flathub's `new-pr` branch, adds `org.musacad.MusaCAD.yml` at the top level and pushes a
branch -- then prints the compare link. `flathub/SUBMISSION_NOTES.md` lists the facts to
draw on (the review points, the pin, the disclosure); the words are yours. The
manifest itself is plain YAML with no comments, as a reviewer expects to see it.

Flathub then builds the manifest, a reviewer looks at it, and on acceptance the app gets its
own repository, `github.com/flathub/org.musacad.MusaCAD`, with you as a maintainer.

The manifest pins the release tag (`tag` and `commit` together, v0.5.0 today); each
release moves it (below).

### Every release after that (automatic)

Two things keep Flathub current once the app is there:

- **Flathub's external-data checker** reads the manifest's `x-checker-data` (a `git`
  source with `tag-pattern: "^v([\d.]+)$"`) and opens a pull request in the app's Flathub
  repository whenever a new `v*` tag appears upstream. You merge it once its build is green
  (auto-merge is not allowed for a new app, so the linter rejects `flathub.json`).
- **`.github/workflows/flathub-release.yml`** in this repository does the same on a pushed
  release tag (or a manual run naming one): it rewrites the pin to the tag and its commit,
  forks the Flathub repository under your account, pushes a `release-<ver>` branch and opens
  the pull request there. It needs the repository secret **`FLATHUB_TOKEN`** (a GitHub token
  of yours with permission to fork and to open pull requests on public repositories, e.g. a
  classic token with `public_repo`); without it, or before the Flathub repository exists,
  the job explains why it did nothing and ends green. It also skips when a pull request for
  that version is already open, so the two paths never duplicate each other.

Each release must also add its `<release>` entry to `org.musacad.MusaCAD.metainfo.xml`
(`docs/RELEASING.md` pre-flight), since the metainfo is read from the tagged source.
