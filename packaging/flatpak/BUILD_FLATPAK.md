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
2. Runs `flatpak-builder` against `org.musacad.MusaCAD.yml`: configures + builds the **release**
   target inside the sandbox, then installs the binary, the app-id-named `.desktop`, the SVG
   icon, and the AppStream `.metainfo.xml` under `/app`.
3. Exports a single-file bundle `packaging/flatpak/MusaCAD-<version>.flatpak`.

## Install + verify

```sh
flatpak install --user -y packaging/flatpak/MusaCAD-0.1.0.flatpak
flatpak run org.musacad.MusaCAD                       # launches the GUI

# Headless load + plot self-test (pass envs through the sandbox):
flatpak run --env=MUSACAD_PLOT_TEST="$HOME/drawing.musa|$HOME/out.pdf|1" org.musacad.MusaCAD
```

## What is bundled / what is not

- The KDE runtime supplies Qt 6 + the platform/imageformats plugins (incl. **qsvg**); nothing
  extra is vendored. Musa CAD's own assets are compiled into the binary (Qt resources).
- **DWG import/export** shells out to an external converter (ODA File Converter / LibreDWG
  `dwg2dxf`) found on `PATH`. That converter is **not** present in the sandbox, so DWG is
  unavailable in the Flatpak by default; built-in **DXF** read/write works. This keeps the
  Flatpak LGPL-clean (no GPL/DWG library linked or shipped). To use a converter installed on
  your system anyway (issue #4): grant the app the Flatpak portal once,
  `flatpak override --user --talk-name=org.freedesktop.Flatpak org.musacad.MusaCAD`, then turn
  on **"Use a converter installed on the host"** in the DWG Setup dialog. Discovery, the
  existence check and the conversion then run on the host through `flatpak-spawn --host`,
  with the converter's scratch files under the app's cache directory (a path the host sees).
  The manifest asks for nothing extra, so the default stays sandboxed.
- File access is limited to `--filesystem=home`; the OpenGL viewport uses `--device=dri`;
  X11 (`fallback-x11`) and Wayland sockets are granted.

## Flathub submission — PREPARED

`flathub/org.musacad.MusaCAD.yml` is the submission manifest: the same build as the local one,
with the tagged release as its source (`tag` + `commit`, updated together for each release).
The AppStream metainfo carries a `<release>` per version and its screenshot URLs resolve on
`main`. Lint both before a submission (the linter ships with `org.flatpak.Builder`):

```sh
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest packaging/flatpak/flathub/org.musacad.MusaCAD.yml
flatpak run --command=flatpak-builder-lint org.flatpak.Builder appstream packaging/flatpak/org.musacad.MusaCAD.metainfo.xml
```

The metainfo validates. The app id is `org.musacad.MusaCAD`, the reverse of the project's
domain (`https://musacad.org/`, the metainfo's homepage), which is what Flathub verifies.
The manifest reports one item that is a decision, not a bug: `finish-args-home-filesystem-access`.
Flathub prefers the file-chooser portal to `--filesystem=home`; Musa CAD needs the folder
around a drawing, not just the picked file -- external references, attached images and the
images a DXF export writes beside it are all found by relative path. State that in the
submission PR as the reason for the exception (the linter's documented route); narrow to
`xdg-documents` only if the reviewers insist.

To submit: fork `github.com/flathub/flathub`, branch from `new-pr`, add
`org.musacad.MusaCAD.yml` (this directory's `flathub/` copy) at the top level, and open the
PR against `new-pr`. After each release update `tag` / `commit` there.
