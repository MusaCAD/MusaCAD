<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Changelog

All notable changes to Musa CAD are recorded here. This project aims to follow
[Semantic Versioning](https://semver.org/).

## Unreleased

Everything below is on `main` and in the per-command table in
[`docs/COMMANDS.md`](docs/COMMANDS.md); the roadmap in
[`docs/ROADMAP.md`](docs/ROADMAP.md) shows what is still open.

### Added
- **Draw primitives** (#23) — `SPLINE` (fit and control-vertex methods), `ELLIPSE` (centre,
  axis-end, rotation, elliptical arcs), `POLYGON`, `POINT`, `XLINE` / `RAY` construction
  lines, `DONUT`, `REVCLOUD`, and the `RECTANGLE` first-corner options (Chamfer / Fillet /
  Width / Dimensions / Area / Rotation).
- **Modify commands** (#27) — `BREAK`, `LENGTHEN`, `ALIGN`, `DIVIDE` / `MEASURE`, `PEDIT`
  (Close / Open / Join / Edit vertex / Spline / Decurve / Reverse / Undo), and `TRIM` /
  `EXTEND` / `FILLET` where the modified object is an arc, circle or polyline.
- **Dimension types** (#28) — `DIMORDINATE`, `DIMJOGGED`, `DIMARC`.
- **Text styles** (#29) — the `STYLE` table (font, height, width factor, oblique), a current
  style, a style picker in the properties palette, native and DXF `STYLE` both ways.
- **Housekeeping and inquiry** (#30) — `UNITS` (formats and precision, used by the readout
  and by `DIST` / `ID` / `AREA` / `LIST`), `PURGE`, `AUDIT`.
- **Blocks** (#25) — `BLOCK` / `INSERT` / `WBLOCK` / `EXPLODE` / `REGEN`; **block
  attributes** (`ATTDEF`, `INSERT` value prompts, `ATTDISP`, `ATTEDIT`); **in-place editing**
  (`REFEDIT` / `REFSET` / `REFCLOSE`).
- **Views, groups, masks, fields** (#33) — named views (`VIEW`), `GROUP` / `UNGROUP` /
  `PICKSTYLE`, `WIPEOUT` (with `WIPEOUTFRAME`), `FIELD` (date, time, file name, login), and
  `HATCH` gradient fills.
- **Snapping and input** (#32) — Insertion, Apparent intersection and Parallel snaps, the
  `OSNAP` settings dialog and `-OSNAP`, `ROTATE` / `SCALE` Copy and Reference options with
  value dialogs and a live ghost, editable geometry fields in the properties palette, and
  GD&T frame cell editing.
- **Interop** (#31) — DXF `TOLERANCE` (GD&T) both ways, DXF `SPLINE` import, the legacy
  `POLYLINE` / `VERTEX` / `SEQEND` form, `ATTDEF` and `INSERT` + `ATTRIB` both ways, and the
  gradient-hatch block.

### Changed
- Plot: fills are drawn as one path per colour, so hatches no longer show hairline seams
  between triangles; a `WIPEOUT` masks on paper as it does on screen.
- The `STRETCH` base-point prompt no longer lags the cursor.

### Fixed
- Undoing an edit twice in a row could leave a duplicate object (stale handles in the
  undo history after a re-creation).
- Opening a drawing into a new tab lost the Standard text style and could crash text
  layout.
- **Windows** (#6, verified on real hardware):
  - The MSVC build compiles again (`localtime_r`, a missing `<algorithm>`, `M_PI`), and
    the `dev` preset (Debug + AddressSanitizer + tests) builds and runs with MSVC
    (`/bigobj`; the Qt DLLs are put on PATH for test discovery, CTest and `cli_check`).
  - No console window behind the application any more: `musacad_app.exe` is a windowed
    program. `musacad.exe` is a new console front-end for scripts that waits and returns
    the documented exit codes, and both are in the installer (see docs/CLI.md).
  - Drawings under a folder with a non-ASCII name (accented or Indic characters) open,
    save and plot; the executable now runs with a UTF-8 code page.
  - `FIELD` `%<Login>%` is filled from `USERNAME`; the developer hooks write to the temp
    directory instead of `/tmp`; the self-test's DWG round trip runs on Windows.
  - ODA File Converter is auto-detected in its `Program Files\ODA` folder, where its
    installer puts it without touching PATH.
  - The Qt-free libraries are compiled with `/utf-8`, so their non-ASCII literals no
    longer depend on the build machine's code page.
  - `.gitattributes` keeps a Windows checkout at LF like the repository.
  - Installer: `--plot` works from the installed copy (the offscreen Qt plugin is
    bundled; a missing plugin can no longer hang a script on a message box), the
    Start-menu entry is created for all users, "Run Musa CAD" on the finish page starts
    the program as the normal user, and the Visual C++ redistributable it ships is
    actually installed.

### Compatibility
Native format **v28**. Files from v0.3.0 (v20) open unchanged; files saved by this build
carry the new tables and entities and need this build or newer.

---

## v0.3.0 — editing and inquiry

Full notes: [`docs/release-notes/v0.3.0.md`](docs/release-notes/v0.3.0.md). **Linux only.**

### Added
- **STRETCH** (#24) — crossing-window vertex move; a dimension whose definition points are
  enclosed re-measures.
- **Inquiry commands** (#30, partial) — `DIST`, `ID`, `AREA`, `LIST`. `PURGE`, `AUDIT` and
  `UNITS` remain.
- **DIMCONTINUE / DIMBASELINE** (#28, partial) — chain or stack dimensions from the previous
  one. DIMORDINATE, DIMJOGGED and DIMARC remain.

### Fixed
- The **AppImage did not bundle Qt's `offscreen` platform plugin**, so `musacad --plot`
  failed inside the packaged artifact (exit 127) while the desktop launch worked.

### Compatibility
Native format unchanged at **v20** — nothing here adds stored state, so v0.2.0 and v0.3.0
files are interchangeable.

---

## v0.2.0 — the annotation release

Full notes: [`docs/release-notes/v0.2.0.md`](docs/release-notes/v0.2.0.md). **Linux only**;
the Windows installer is built and verified separately on real hardware (#6).

### Added
- **Dimension tolerances, fit classes, prefixes and the basic box** (#7) — symmetric,
  limits (stacked), basic (boxed) and reference modes. The measured value is still computed
  from the definition points and can never be authored.
- **Dimension text override** (#20) — AutoCAD's `<>` field: `<> H7` tracks the geometry;
  an override without `<>` replaces the value, shown explicitly rather than silently.
- **Dimension text position** (#21) — a grip on the text of every dimension type, a
  connector leader when the label leaves its dimension line, and "home text" in the
  Properties palette.
- **ISO 129-1 narrow-dimension fallback** (#12) — the value moves outside and the
  arrowheads flip inward when they no longer fit, decided independently for text and arrows.
- **GD&T** (#8) — feature control frames and datum feature symbols sharing DIMSTYLE, so
  GD&T annotation matches the drawing's dimensions automatically. `TOLERANCE`, `DATUM`.
- **26 drafting symbols in the stroke font** (#9) — hole callouts, the GD&T characteristic
  set, material-condition modifiers, feature-form symbols; reachable via `%%b`/`%%h`/`%%v`
  and the general `\U+XXXX` escape.
- **TABLE entity + TABLESTYLE** (#22) — BOMs, revision blocks, hole schedules; merged
  cells, per-cell alignment, style-driven text heights. `TABLE`/`TB`.
- **Command line** (#11) — `musacad <drawing>`, `--check`, and headless `--plot` with
  paper/orientation/scale/window options. Exit codes 0/1/2/3. See `docs/CLI.md`.
- **Raster IMAGE entity** (#10, **partial**) — external or base64-embedded payloads;
  selectable, movable and plotted. Viewport display, IMAGEATTACH/IMAGECLIP and DXF deferred.
- `docs/ROADMAP.md` — a grouped survey of what is not yet implemented (issues #20–#33).

### Fixed
- **Plotted text ignored its lineweight** (#19). **Behaviour change:** text now prints at
  its resolved weight instead of a hairline; on-screen appearance is unchanged.
- A **hatch loaded from a file was never spatially indexed**, so it could not be picked,
  hovered, window-selected or erased.
- **Fifteen printable ASCII characters had no glyph**, so e.g. `50% FULL` plotted as
  `50 FULL`.
- The **basic-dimension frame** drew its lower edge on the dimension line.

### Compatibility
Native format **v20** (was v14); every older version still loads, each with a regression
test. Files written by v0.2.0 cannot be opened by v0.1.0. GD&T, tables and images are
native-only — the DXF gaps are stated, not faked.

---

## v0.1.0 — first public preview

An **honest, early v0.1.0**: a capable AutoCAD-style 2D drafting application built on a
multi-threaded, GPU-accelerated core — useful for real 2D work, but young. Not "stable" or
"feature-complete." Licensed under **LGPL-3.0-or-later** (see [`LICENSE`](LICENSE)).

### What Musa CAD does
- **Draw:** line, polyline (with per-vertex arc bulges), circle, arc, rectangle.
- **Modify:** erase, move, copy, mirror, offset, rotate, scale, array (rectangular + polar),
  trim, extend, fillet (incl. polyline-corner arcs), chamfer.
- **Precision:** object snaps (OSNAP), ortho/polar tracking, grid, and **grip** direct
  manipulation; dynamic input (DYN) mirroring the command line.
- **Layers & properties:** layer manager (on/freeze/lock, colour/linetype/lineweight,
  ByLayer), current-layer control, and a context-sensitive **Properties palette**.
- **Annotation:** single-line TEXT, paragraph **MTEXT**, **QLEADER**; dimensions
  (linear, aligned, radius, diameter, angular, and a smart `DIM`) with editable dimension
  styles and per-dimension overrides; **TrueType/OpenType fonts** plus SHX-name → stroke/TTF
  substitution.
- **Blocks:** block definitions + `INSERT` references, including nested blocks.
- **Files:** native `.musa` format (round-trips every entity family); **DXF import/export**;
  **DWG import/export** via an external converter (see below); **SPLINE** and **ELLIPSE** now
  import from DXF/DWG (de Boor NURBS evaluation).
- **Plot:** vector **PDF** output and physical **printer** support — paper/orientation/area
  (Display/Extents/Window)/scale/lineweights/CTB plot styles (None/Mono/Grayscale)/copies,
  with saved page setups persisted in the drawing.
- **Branding/About:** application/window icon and a Help → About dialog.

### Known limitations & staged work (honest scope)
- **DWG/DXF import fidelity:** unsupported entities are **catalogued and reported**, not
  silently dropped — currently skipped: `HATCH`, `SOLID`, `POINT`, dimensions/leaders inside
  blocks, and proxy/exotic entities. (`SPLINE`/`ELLIPSE` are now imported.)
- **Fonts:** SHX fonts render via a faithful TTF/stroke **substitution**; true SHX
  shape-file parsing is staged. A first-class text-style table is staged.
- **MTEXT:** inline per-character formatting is flattened to plain text on import.
- **Plot:** model space only (no paper-space layouts/viewports); built-in CTB styles only
  (no editable `.ctb` pen tables); no plot stamp / batch publish / raster output.
- **Properties / dialogs:** some deep property groups and per-command modal/dynamic-input
  dialogs are staged; a few numeric-geometry edits are read-only.
- **Scope:** Musa CAD is a **2D** engine; 3D B-rep is not part of this release.

See [`docs/TODO.md`](docs/TODO.md) for the full deferred-work backlog (with rationale).

### DWG support & licensing
DWG import/export is performed by an **external converter** (LibreDWG `dwg2dxf` or the ODA
File Converter) invoked as a **separate process** — no DWG library is linked into or shipped
with Musa CAD, which keeps Musa CAD LGPL-clean. Install a converter separately to enable DWG
(see [`docs/BUILD.md`](docs/BUILD.md)). Dependency licenses and the GPL-boundary evidence are
in [`docs/THIRD_PARTY_LICENSES.md`](docs/THIRD_PARTY_LICENSES.md).

### Build
Build from source per [`docs/BUILD.md`](docs/BUILD.md) (CMake + a C++23 compiler + Qt 6;
optional external DWG converter for DWG support).
