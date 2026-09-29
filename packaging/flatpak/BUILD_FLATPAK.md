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

## Flathub

Musa CAD is published on Flathub as
[`org.musacad.MusaCAD`](https://flathub.org/apps/org.musacad.MusaCAD). The app's manifest lives
in its own repository, [flathub/org.musacad.MusaCAD](https://github.com/flathub/org.musacad.MusaCAD);
`flathub/org.musacad.MusaCAD.yml` here is the upstream copy of it: the same module as the local
manifest, with the release tag as its source (`tag` and `commit`, moved together for each
release). The AppStream metainfo carries a `<release>` per version and its screenshot URLs
resolve on `main`. Lint both before proposing an update (the linter ships with
`org.flatpak.Builder`; its sandbox needs to see the files):

```sh
flatpak run --filesystem="$PWD" --command=flatpak-builder-lint org.flatpak.Builder \
  --exceptions --exceptions-repo stable manifest "$PWD/packaging/flatpak/flathub/org.musacad.MusaCAD.yml"
flatpak run --filesystem="$PWD" --command=flatpak-builder-lint org.flatpak.Builder \
  appstream "$PWD/packaging/flatpak/org.musacad.MusaCAD.metainfo.xml"
```

The app id is `org.musacad.MusaCAD`, the reverse of the project's domain
(`https://musacad.org/`, the metainfo's homepage), which Flathub verifies through
`https://musacad.org/.well-known/org.flathub.VerifiedApps.txt`. The manifest asks for no
filesystem permission (see above) and has no build commands of its own: `cmake-ninja` plus the
project's `install()` rules, with the desktop entry already named and pointing at the app-id
icon.

### A release on Flathub

1. Before tagging, add the release's `<release>` entry to `org.musacad.MusaCAD.metainfo.xml`
   (`docs/RELEASING.md` pre-flight): Flathub reads the metainfo from the tagged source.
2. Move the pin in `flathub/org.musacad.MusaCAD.yml` to the new tag and its commit.
3. Open a pull request in `flathub/org.musacad.MusaCAD` carrying that manifest. Flathub's bot
   builds it for x86_64 and aarch64 and reports on the pull request; merge it once the test
   build is green, and Flathub publishes the build within a few hours.

Two things open step 3's pull request without anyone doing it by hand:

- **Flathub's external-data checker** reads the manifest's `x-checker-data` (a `git`
  source with `tag-pattern: "^v([\d.]+)$"`) and opens the pull request whenever a new `v*`
  tag appears upstream (auto-merge is not allowed for a new app, so the linter rejects
  `flathub.json`).
- **`.github/workflows/flathub-release.yml`** in this repository does the same on a pushed
  release tag (or a manual run naming one): it rewrites the pin to the tag and its commit,
  forks the Flathub repository under the token owner's account, pushes a `release-<ver>`
  branch and opens the pull request there. It needs the repository secret **`FLATHUB_TOKEN`**
  (a maintainer's GitHub token with permission to fork and to open pull requests on public
  repositories, e.g. a classic token with `public_repo`); without it the job explains why it
  did nothing and ends green. It also skips when a pull request for that version is already
  open, so the two paths never duplicate each other.

Installed copies learn about the new version on their own: the app checks Flathub once a day
and shows the update in its status bar (Options turns the check off).
