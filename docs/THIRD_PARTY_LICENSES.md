<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Third-party licenses & compliance evidence

Musa CAD is **LGPL-3.0-or-later** (see [`COPYING`](../COPYING) / [`COPYING.LESSER`](../COPYING.LESSER)).
This document inventories every dependency, how Musa CAD uses it, and a per-dependency
compatibility verdict for LGPL-3 distribution.

## Summary

| Dependency | Version | License | How Musa CAD uses it | Verdict (LGPL-3 distribution) |
|---|---|---|---|---|
| **Qt 6** (Core, Gui, Widgets, OpenGL, OpenGLWidgets, PrintSupport; DBus transitively; SVG image/icon plugins at runtime) | system Qt 6 | **LGPL-3.0** (open-source edition) | **Dynamically linked** to system shared libraries | ✅ Compatible |
| **Vulkan** (loader + headers) | system | Apache-2.0 (loader); MIT/Apache-2.0 (headers) | Build-time located (`find_package`); **not linked into the shipped binary** | ✅ Compatible |
| **Catch2** | v3.5.4 | BSL-1.0 | **Test-only** (`FetchContent`, builds `musacad_tests`); not in the shipped app | ✅ Compatible |
| **pthreads / glibc** | system | LGPL-2.1-or-later (glibc) | System C runtime (`Threads::Threads`) | ✅ Compatible (system library) |
| **System fonts** (via Qt `QFontDatabase`/fontconfig/FreeType) | system | (Qt transitive; fonts are user/OS-provided) | Runtime, through Qt; no font files shipped | ✅ N/A to Musa's binary |
| **LibreDWG / ODA File Converter** (DWG↔DXF) | external | **GPL-3.0** (LibreDWG) / proprietary EULA (ODA) | **External process only** — invoked via `QProcess`, discovered at runtime; **never linked, vendored, or shipped** | ✅ Does not affect Musa's LGPL (see scan below) |

## Notes per dependency

### Qt 6 — LGPL-3.0, dynamically linked
Musa CAD uses the open-source (LGPL-3) edition of Qt 6 and **links it dynamically** against
the system shared libraries (verified by `ldd`, below). LGPL-3 then permits Musa CAD's own
LGPL-3 licensing and permits closed-source downstream use, provided the user can relink
against a modified Qt — which dynamic linking satisfies. The Qt SVG **icon/image plugins**
(used for the branding logo and the ribbon icons) are loaded at runtime from the system Qt
plugin path and are likewise LGPL-3. Obligations to honour: keep Qt's copyright/license
notices; ship/allow a replaceable (dynamically linked) Qt; do not use any Qt module that is
GPL-only (none are used — the modules above are LGPL-3).

### First-party assets — no third-party content
All shipped image assets are **authored as part of Musa CAD** and carry the project's
LGPL-3-or-later license: the branding logo (`assets/branding/`) and the **49 ribbon command
icons** (`assets/ribbon/*.svg`, added in Ribbon Phase A). The ribbon icons are hand-drawn
SVG primitives — **no third-party icon set is bundled, vendored, or linked**, so this phase
adds **nothing** to the dependency inventory above. (Icon iconography follows common CAD
*concepts* — e.g. a paintbrush for MATCHPROP — which are conventions, not protected
expression; the drawings themselves are original.)

### Vulkan — permissive, not in the shipped artifact
`find_package(Vulkan)` (optional) validates the GPU-backend seam at configure time when an SDK is
present, but the shipped viewport renders through **Qt's OpenGL** path; nothing links the `Vulkan::`
targets and `libvulkan` does **not** appear in the binary's dynamic dependencies (see scan). Builds
on hosts without a Vulkan SDK (e.g. the Windows installer CI) are byte-identical. The Vulkan loader
is Apache-2.0 and the headers are MIT/Apache-2.0 — permissive and LGPL-compatible regardless.

### Catch2 — test-only
Catch2 is fetched (`FetchContent`, pinned `v3.5.4`) only to build the `musacad_tests` target.
It is **not** part of the distributable application. BSL-1.0 is permissive and would be
compatible even if shipped.

### Distribution bundles (AppImage / Flatpak / Windows installer)
The v0.1.0 release artifacts bundle a Qt runtime so users need not install Qt. Each is
LGPL-clean — Qt 6 stays **dynamically linked LGPL-3.0**, and no GPL/DWG library is ever pulled in:

- **AppImage** (`linuxdeploy` + `linuxdeploy-plugin-qt`): bundles the Qt 6 LGPL-3 libraries
  (Core, Gui, Widgets, OpenGL, PrintSupport, Svg, DBus, XcbQpa) + the `qsvg`/`qsvgicon`/`qxcb`
  plugins, plus their transitive system dependencies (glib, ICU, libpng, freetype, harfbuzz,
  gnutls/gcrypt/nettle, cups, xcb-*, zstd, etc.). All of these are **LGPL or permissive**
  (MIT/BSD/Apache/zlib) and LGPL-3-compatible; **none are GPL and none are DWG-related** (verified
  by `ls AppDir/usr/lib` + the scan below).
- **Flatpak**: Qt 6 comes from the **`org.kde.Platform//6.10`** runtime (LGPL-3); nothing extra
  is vendored into the app.
- **Windows installer** (`windeployqt` + NSIS): bundles the Qt 6 LGPL-3 DLLs + plugins and the
  Microsoft Visual C++ runtime redistributable (redistributable under Microsoft's terms).
- **Deploy tools** (`linuxdeploy`, `appimagetool`, `flatpak-builder`, `windeployqt`, NSIS) are
  **build-time only** — they assemble the bundle and are not shipped inside it.

LibreDWG / ODA remain **external-process-only** in every bundle (not present in `AppDir/usr/lib`,
absent from the sandbox, never in the Windows staging tree). DWG support stays opt-in via a
user-installed converter on `PATH`.

---

## LibreDWG / GPL boundary scan (load-bearing compliance check)

DWG import/export is intentionally performed by an **external converter** (LibreDWG `dwg2dxf`,
GPL-3.0, or the proprietary ODA File Converter) kept at a **process boundary** so that no
GPL-licensed (or proprietary-EULA) code is linked into Musa CAD. This preserves Musa CAD's
LGPL-3 licensing. Evidence for the v0.1.0 release:

1. **No DWG/GPL library in any build/link target.**
   `grep -rniE "libredwg|dwg2dxf|libdwg|ODA" CMakeLists.txt src/*/CMakeLists.txt tests/CMakeLists.txt`
   → **no matches.** No `find_package`/`FetchContent`/`target_link_libraries` references a DWG
   or GPL library. The only `FetchContent` dependency is Catch2 (test-only).

2. **No GPL/DWG library in the built binary's dynamic dependencies.**
   `ldd build/release/bin/musacad_app | grep -iE "dwg|gpl|libredwg"` → **no matches.**
   The binary links Qt 6 (LGPL-3) and the system C/C++ runtime only; `libvulkan` is absent.

3. **The converter is invoked only as a subprocess, discovered at runtime.**
   `src/ui/dwg_converter.cpp` runs the converter via `QProcess` (`proc.start(program, args)`),
   resolving the executable from `PATH`/a user-configured path at runtime. Musa CAD ships no
   converter binary and contains no converter code.

**Finding: PASS — no GPL linkage.** LibreDWG/ODA are used as an
external process only; they are not linked, vendored, fetched, or shipped, so Musa CAD's
LGPL-3 distribution is not affected by their GPL/EULA terms. A user who wants DWG support
installs the converter separately (see [`BUILD.md`](BUILD.md)).

## Reproducing the scan
```sh
# 1. No DWG/GPL lib in the build graph:
grep -rniE "libredwg|dwg2dxf|libdwg|ODA" CMakeLists.txt src/*/CMakeLists.txt tests/CMakeLists.txt

# 2. No GPL/DWG lib linked into the artifact:
ldd build/release/bin/musacad_app | grep -iE "dwg|gpl|libredwg|vulkan" || echo "none"

# 3. Converter is a subprocess:
grep -n "QProcess\|\.start(" src/ui/dwg_converter.cpp
```
