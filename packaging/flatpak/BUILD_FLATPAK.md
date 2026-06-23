# Building the Musa CAD Flatpak

Produces a single-file `MusaCAD-<version>.flatpak` bundle that users install with
`flatpak install --user MusaCAD-<version>.flatpak` and run with
`flatpak run com.musacad.MusaCAD`.

Built against the **KDE 6.10** runtime (`org.kde.Platform` // `org.kde.Sdk`), which provides
Qt 6. The app is compiled from source inside the sandbox (Flathub-style).

## Prerequisites (all user-level, no root)

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y flathub org.kde.Platform//6.10 org.kde.Sdk//6.10 org.flatpak.Builder
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
2. Runs `flatpak-builder` against `com.musacad.MusaCAD.yml`: configures + builds the **release**
   target inside the sandbox, then installs the binary, the app-id-named `.desktop`, the SVG
   icon, and the AppStream `.metainfo.xml` under `/app`.
3. Exports a single-file bundle `packaging/flatpak/MusaCAD-<version>.flatpak`.

## Install + verify

```sh
flatpak install --user -y packaging/flatpak/MusaCAD-0.1.0.flatpak
flatpak run com.musacad.MusaCAD                       # launches the GUI

# Headless load + plot self-test (pass envs through the sandbox):
flatpak run --env=MUSACAD_PLOT_TEST="$HOME/drawing.musa|$HOME/out.pdf|1" com.musacad.MusaCAD
```

## What is bundled / what is not

- The KDE runtime supplies Qt 6 + the platform/imageformats plugins (incl. **qsvg**); nothing
  extra is vendored. Musa CAD's own assets are compiled into the binary (Qt resources).
- **DWG import/export** shells out to an external converter (ODA File Converter / LibreDWG
  `dwg2dxf`) found on `PATH`. That converter is **not** present in the sandbox, so DWG is
  unavailable in the Flatpak by default; built-in **DXF** read/write works. This keeps the
  Flatpak LGPL-clean (no GPL/DWG library linked or shipped).
- File access is limited to `--filesystem=home`; the OpenGL viewport uses `--device=dri`;
  X11 (`fallback-x11`) and Wayland sockets are granted.

## Flathub submission — STAGED

The manifest + AppStream metainfo are ready, but submission to Flathub is **deferred until after
the GitHub release** (tracked in `docs/TODO.md`). For submission:

1. Swap the manifest's `dir` source for the tagged release:
   ```yaml
   sources:
     - type: git
       url: https://github.com/MusaCAD/MusaCAD.git
       tag: v0.1.0
       commit: <full-sha>
   ```
2. Open a PR adding `com.musacad.MusaCAD.yml` to `github.com/flathub/flathub` (new-app branch).
3. Confirm the AppStream metainfo passes `flatpak run org.flatpak.Builder --validate` / the
   Flathub linter, and that the screenshot URLs resolve on `main`.
