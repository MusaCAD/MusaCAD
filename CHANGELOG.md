<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Changelog

All notable changes to Musa CAD are recorded here. This project aims to follow
[Semantic Versioning](https://semver.org/).

## Unreleased

### Fixed
- FILLET and CHAMFER rebuilt the trimmed lines, the arc, the bevel and the polyline without
  their properties, so the results landed on the current layer with default colour and
  linetype. They keep the objects' layer, colour, linetype and linetype scale; the arc or
  bevel takes the objects' layer when both share it.

### Added
- **The ribbon laid out as AutoCAD's** -- the Drafting & Annotation tabs (Home, Insert,
  Annotate, Parametric, View, Manage, Output), their panels and groupings: large tools with
  stacked small ones, drop-downs under the labels (Circle ▾ with its six methods, Arc ▾ with
  its eleven, Linear ▾ with the dimension kinds, Zoom ▾ …), slide-outs behind the panel
  titles ("Draw ▾"), dialog launchers, and the file operations in the application menu on
  the Musa mark. The buttons take AutoCAD's two split shapes: a large one runs its command
  from the icon and opens its menu from the label, with the chevron under it; a small one
  keeps the chevron at its right. Home's stacked tools (Rectangle, Ellipse, Hatch; Trim,
  Fillet, Array; the layer, block, group, utility and clipboard tools) show as bare icons
  with their names in the tooltips, as AutoCAD lays them out, so the whole Home tab fits
  a 1500 px window. The Properties panel gains the colour, linetype and lineweight
  controls, which edit the selection or, with nothing selected, set what new objects are
  drawn with (the current entity properties). Tools Musa CAD lacks stand in AutoCAD's
  place as disabled buttons whose tooltips say so; the collapse behaviour and the
  contextual tabs are unchanged. The application button and the Quick Access Toolbar sit
  at the left of the strip.
- **Download ODA File Converter** -- DWG Setup (and the message shown when a DWG has no
  converter) can fetch the free converter from the Open Design Alliance into Musa CAD's
  own data directory, after you accept its terms, and set it up; "Remove downloaded"
  deletes it. Linux, Windows and macOS builds are supported; the Flatpak downloads too and
  runs it through the portal under Wayland.
- **ARC construction methods** (#34) -- `[Center]` at the first prompt and `[Center/End]` at
  the second: start-centre-end / angle / chord length, start-end-centre / angle / direction /
  radius, the centre first, and **Continue** (Enter starts tangent from the last line or arc).
  Holding **Ctrl** at a pick draws the other way round, as AutoCAD's prompts say; the rubber
  band shows the arc the click will make.
- **ALIGN pairs** (#52) -- one pair (Enter at the second source point) moves the objects;
  after two pairs `Specify third source point or <continue>:`; a rubber line runs from each
  source point to its destination.
- **MIRROR keeps text readable** (#52) -- as AutoCAD's default `MIRRTEXT` 0 does: a mirrored
  text lands in the reflected place, turned round and re-justified so it still reads the
  right way; `MIRRTEXT 1` reflects it. **BREAK** takes `@` for a single break point and
  **BREAKATPOINT** asks `Specify break point:` after the selection.
- **POLYGON and XLINE details** (#39) -- POLYGON remembers the side count (POLYSIDES) and
  the Inscribed / Circumscribed choice, limits the sides to 3..1024, stands a typed radius
  on a flat bottom edge, and shows the polygon while an edge is picked; XLINE gains
  `[Offset]` (a distance or Through, along a line, construction line, ray or polyline
  segment), `Enter angle of xline (0) or [Reference]:`, more bisectors from one vertex, and
  XLINE and RAY rubber-band the line the click would make.
- **OFFSET options** (#50) -- `[Through/Erase/Layer]` at the distance prompt (a value, two
  points, or Through), the last distance as the default, `[Exit/Undo]` and
  `[Exit/Multiple/Undo]` while offsetting (Multiple steps out from the offset just made),
  one undo step per offset, and the settings echoed at the start and kept for the session.
- **MOVE and COPY options** (#47) -- `[Displacement]` with the last vector as the default,
  Enter at the second point using the first point as the displacement, COPY's `[mOde]`
  (Single / Multiple), `[Array]` with `[Fit]`, `[Undo]` and `[Exit]`, and one undo step per
  copy.
- **FILLET and CHAMFER as AutoCAD runs them** (#49) -- the objects are selected first and
  the settings are options: `[Undo/Polyline/Radius/Trim/Multiple]` and
  `[Undo/Polyline/Distance/Angle/Trim/mEthod/Multiple]`, with the current settings echoed at
  the start and remembered for the session. **Multiple** repeats with one undo step per
  corner, **Undo** takes the last one back, **Trim / No trim** decides whether the objects are
  trimmed or only the arc / bevel is added, **Polyline** treats every corner of a polyline,
  and **Shift** at the second pick makes a sharp corner.
- **PLINE Arc mode** (#37, the geometry) -- `[Arc/Close/Length/Undo]` in line mode and
  `[Angle/CEnter/CLose/Direction/Line/Radius/Second pt/Undo]` in arc mode, each sub-step as
  AutoCAD prompts it: an arc is tangent to the previous segment unless a direction, centre,
  radius or second point says otherwise, Ctrl bends it the other way, CLose returns to the
  start with an arc, and Length continues along the last segment. The band shows the arc the
  click will make. Width and Halfwidth wait on polyline widths.
- **RECTANG details** (#36, the parts that need no polyline width) -- `Specify other corner
  point or [Area/Dimensions/Rotation]:`; the length, width, area and rotation are remembered
  as the next defaults (the rotation stays in force, as in AutoCAD); `Specify rotation
  angle or [Pick points]`; `Current rectangle modes: Fillet=…` at the start; the Area option
  means the finished shape's area, corner cut-outs included; the rubber band shows the
  rounded or chamfered corners. Width, Elevation and Thickness wait on #37.
- **Direct distance entry** -- at any point prompt a bare number is a distance along the
  cursor's direction from the last point (`10` + Enter), with ortho and polar applied; a
  bare `@` is the last point.
- **LINE Close and Continue** -- `[Close/Undo]` once two segments exist; Enter at the first
  prompt continues from the last line or arc (tangent to an arc, asking a length).
- **CIRCLE construction methods** (#35) -- `[3P/2P/Ttr (tan tan radius)]` at the first
  prompt, plus `TTT` (tangent to three objects); the tangent circles are solved against
  lines, circles, arcs, construction lines and polyline segments, on the branch nearest the
  picks ("Circle does not exist." otherwise). The last radius is the next default; a pick
  at the diameter prompt is the diameter (it used to be doubled); the rubber band and the
  Dynamic Input field follow the chosen method.
- **ROTATE and SCALE bands** -- the selection is previewed by the engine while you drag:
  every kind (text, hatches, dimensions, blocks) at the current zoom, exactly where the
  click will put it; the live angle or factor shows at the cursor in the drawing's units,
  and typing replaces it. The factor follows AutoCAD's rule: the cursor's distance from
  the base point in drawing units.
- **ROTATE and SCALE Reference** as AutoCAD words it: the reference by a value or two
  points, then `Specify the new angle or [Points] <0>:` / `Specify new length or [Points]
  <1.0000>:` with the value, a point from the base, or two points; `Specify scale factor
  or [Copy/Reference]:` offers both options from the start.
- **Tiled model-space viewports** (#33) -- `VPORTS` splits the window into viewports that
  each pan and zoom on their own (2, 3, 4, SIngle, Join, and Save / Restore / Delete / `?`
  for named configurations); the View tab's Viewport Configuration list applies a standard
  layout. Click a viewport to make it current. Saved in the drawing (format v35) and as the
  DXF `VPORT` table.
- **Attribute dialogs** (#25) — `EATTEDIT` (or a double-click on a block reference) edits a
  reference's attribute values in the Enhanced Attribute Editor; `BATTMAN` edits, reorders
  and removes a block's attribute definitions and syncs every reference by tag. The Insert
  tab gains real Block and Block Definition panels.
- **Per-viewport layer freezing** (#26) — `VPLAYER` freezes and thaws layers in the current,
  all or a picked viewport (Reset, Newfrz, Vpvisdflt, `?`); the Layer Properties Manager
  gains "New VP Freeze" and, while you are in a viewport, "VP Freeze" columns. The lists
  are saved in the drawing (format v34) and travel through DXF (`VIEWPORT` 331, `LAYER` flag 2).

- **DWG inside the Flatpak** (#4) -- DWG Setup gains "Use a converter installed on the host":
  after a one-time `flatpak override --user --talk-name=org.freedesktop.Flatpak
  org.musacad.MusaCAD`, the sandboxed app finds and runs the ODA File Converter or
  LibreDWG installed on your system. The default stays sandboxed, and the app now explains
  the situation instead of only reporting "no converter".
- **macOS** (#2) -- the tag workflow now builds `MusaCAD.app` on an Apple Silicon runner and
  attaches `MusaCAD-<ver>-arm64.dmg` to the release. The command line works there; the
  viewport needs OpenGL 4.5, which macOS does not provide, and the app now says so at startup
  (on any system whose driver falls short) instead of opening a blank window.
- The Flatpak app id is `org.musacad.MusaCAD`, the reverse of the project's domain
  (musacad.org); a locally installed `com.musacad.MusaCAD` is a different app to Flatpak and
  can be uninstalled. The Flatpak builds on the KDE 6.11 runtime.
- **Flathub** (#1) -- the submission is prepared (`packaging/flatpak/flathub/`: the manifest,
  a helper that stages the pull request for you, and a draft of its body), and every release
  tag now opens the update pull request on Flathub through a workflow, with Flathub's own
  checker as the second path.

### Changed
- The tree builds with Apple's libc++: number parsing no longer depends on floating-point
  `std::from_chars` (and is locale-safe everywhere), and the engine's threads use a small
  `jthread` shim where the standard library has none.

### Fixed
- MSVC builds warnings-as-errors again (#5); the Flatpak builds with the KDE 6.11 SDK's GCC 15.

## v0.4.0 — sheets and symbols

Full notes: [`docs/release-notes/v0.4.0.md`](docs/release-notes/v0.4.0.md). Linux AppImage,
Flatpak and, after verification on real hardware (#6), the Windows installer.

Everything below is in the per-command table in
[`docs/COMMANDS.md`](docs/COMMANDS.md); the roadmap in
[`docs/ROADMAP.md`](docs/ROADMAP.md) shows what is still open.

### Added
- **Editing through viewports** (#26) — `MSPACE` (or a double-click inside a viewport) edits
  the model at the viewport's scale; `PSPACE` returns to the sheet and the view you left
  becomes the viewport's view.
- **External references** (#25) — `XREF` attaches a `.musa` or `.dxf` as a block that follows
  its file (Detach, Reload, `?`); xrefs are re-read whenever the drawing opens.
- **Polygonal image clips** (#10) — `IMAGECLIP` New boundary > Polygonal.
- **DXF fidelity** (#31, #10) — every record carries a handle, with `$HANDSEED`, the
  `BLOCK_RECORD` table and BLOCKS before ENTITIES; raster images travel as `IMAGE` /
  `IMAGEDEF` with the classes and dictionary AutoCAD expects (an embedded image is written
  beside the DXF).
- **Layouts, paper space and viewports** (#26) — a layout table with page setups, paper-space
  entities per layout (only the active space is drawn, picked and edited), the sheet drawn
  under a layout, layout tabs (click to switch, right-click for New / Rename / Delete),
  `LAYOUT` / `MODEL` / `PSPACE`, `PLOT` of a layout at 1:1, and `MVIEW` viewports that show
  model space at a scale (two corners or Fit; ON / OFF / Scale / Center; corner grips).
  `MSPACE` (editing model space through a viewport) is not there yet.
- **Raster images on screen** (#10) — placed images draw in the viewport (a textured-quad
  pipeline with a per-definition texture cache); `IMAGEATTACH` (embedded or referenced from
  the drawing's folder, 8 MB embed limit), `IMAGECLIP` (rectangular), `IMAGEFRAME`.
- **Polyline grips** (#32) — a midpoint grip per segment (drag moves a straight segment or
  reshapes an arc), and a right-click grip menu: Add Vertex, Remove Vertex, Convert to Arc,
  Convert to Line.
- **Release publishing** (#3) — pushing a `v*` tag now creates the GitHub release and attaches
  the AppImage and the Windows installer from the tag builds.
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
  - The on-canvas command entry and its suggestion list drew an empty box: the face
    for them was the first font in the system list, which on Windows is a raster font
    without outlines. The platform's UI font is used now, and a face without outlines
    is never picked.
  - Raster images: on Intel graphics only the first image of a frame drew (the driver
    renames a vertex buffer that is refilled while a draw is pending). The images of a
    frame are now uploaded once and drawn from their offsets.
  - The `dev` preset's test build compiles with MSVC's debug STL (the `ImageInstance`
    size ceiling is written in terms of the vector header size).
- Running a developer hook (self-test, UI dump, smoke run, screenshot captures) no
  longer overwrites the saved Dynamic Input preference, so a first real launch after
  one comes up with DYN on as intended.

### Compatibility
Native format **v33**. Files from v0.3.0 (v20) open unchanged; files saved by this build
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
