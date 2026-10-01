<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
<!-- Copyright (C) 2026 Pranay Kiran -->

# Changelog

All notable changes to Musa CAD are recorded here. This project aims to follow
[Semantic Versioning](https://semver.org/).

## Unreleased

### Added
- **On Flathub** -- `flatpak install flathub org.musacad.MusaCAD` (#1).
- **Update check** -- once a day, in the background, Musa CAD asks where it was installed
  from for the latest version: Flathub for the Flatpak, the GitHub release for the AppImage,
  the Windows installer and the macOS disk image. A newer version appears as a small
  **Update available** note in the status bar -- no window over the drawing, no focus taken.
  Clicking it shows what's new and the one step that updates this copy: the
  `flatpak update org.musacad.MusaCAD` command to copy, or the AppImage / installer / disk
  image to download. **Skip This Version** silences that release; **Check for Updates** in
  the application menu asks on demand. Builds from source do not check on their own.
- **Windows updates in place** -- on the installed Windows package the update window has
  **Download and Install**: Musa CAD downloads the new setup from the release, checks it
  (the size and SHA-256 the release publishes, and that the file is a Windows program
  carrying the announced version; a valid signature too once the program is signed), asks
  to save unsaved drawings, and runs the setup silently over this installation -- Windows
  asks once for administrator permission. The setup waits for Musa CAD to exit, updates
  the same folder and starts the new version. A setup run by hand finds an existing
  installation the same way: it says so on its first page, updates it where it is (no
  folder to choose), and keeps the file types the previous version registered.
- **OPTIONS** (`OP`, and **Options** in the application menu) -- the update check and the
  performance overlay.
- **Polyline widths** (#37) -- a polyline carries a starting and an ending width for every
  segment and is drawn as a filled band: straight segments as trapezoids mitred where two
  of one width meet, arcs as ring sectors, tapers running evenly along either. PLINE has
  AutoCAD's **Width** and **Halfwidth** in line and arc mode (`Specify starting width
  <0.0000>:`, `Specify ending width <start>:`, a typed value or a point's distance from
  the last vertex), the ending width staying in force for the segments and the polylines
  that follow (`PLINEWID`, echoed as `Current line-width is ...`). A wide polyline is
  picked anywhere on its band; MOVE, COPY, ROTATE, MIRROR, STRETCH and ARRAY carry the
  widths, SCALE scales them, TRIM / BREAK / EXTEND / FILLET / CHAMFER / OFFSET hand them to
  what they make (a taper keeps its slope), JOIN keeps each source's and EXPLODE drops
  them with AutoCAD's notice. LIST reports them, the Properties palette has **Global
  width**, **Elevation** and **Thickness** rows and MATCHPROP's Polyline setting copies
  the width. Inside a block a wide polyline is filled where the block is inserted.
- **FILL / FILLMODE** -- wide polylines are outlined and hatches hidden when it is off;
  saved with the drawing.
- **RECTANG** `[Chamfer/Elevation/Fillet/Thickness/Width]` (#36) -- Width draws the
  rectangle as a wide polyline; Elevation and Thickness are kept on the polyline and saved
  (DXF 38 / 39). All three stay in force for later rectangles and appear in `Current
  rectangle modes:`.
- **PEDIT Width** and the vertex editor's **Width** (#52) -- one width for every segment,
  or a starting and an ending width for the segment leaving a vertex. Reverse turns a
  taper round with its segment; inserting or deleting a vertex keeps the widths in step.
- **Dynamic Input on PLINE** -- the length and angle fields LINE has, measured from the
  last vertex, on every next-point pick.
- DONUT diameters can be shown by two points.

- **Object snaps for one pick** (#66) -- at any prompt that asks for a point, `END`, `MID`,
  `CEN`, `GCEN`, `NOD`, `QUA`, `INT`, `EXT`, `INS`, `PER`, `TAN`, `NEA`, `APP` or `PAR`
  typed instead of the point snaps that pick only, whatever the running snaps are, and
  `NON` switches them off for it. The prompt shows the mode (`_end of`); a pick with no
  such point under the cursor is refused and asked again. **Shift + right-click** opens
  the object snap menu.
- **FROM**, **M2P** / **MTP**, **TT** and **TK** -- a point at an offset from a base
  point, the middle of two points, a temporary tracking point the cursor locks onto
  (horizontally and vertically), and a chain of orthogonal moves.
- **Extension** object snap (`EXT`) -- the end of a line, an arc or an open polyline is
  acquired by passing the cursor over it; the snap then runs along the line carried on
  past it (or round the arc's circle), with a dashed path from the end, and finds the
  crossing of two acquired lines.
- **OSMODE**; `-OSNAP` offers the modes in force as its default.
- **OFFSET** (#50) offsets construction lines and rays (to one of the same kind), and
  ellipses, elliptical arcs and splines (to a spline a constant distance away, as AutoCAD
  does). The offset a click would make follows the cursor at the side prompt, through
  the through point with Through. **OFFSETGAPTYPE** 1 and 2 round or bevel the gaps an
  offset opens at the outside corners of a polyline.

- **Polar tracking as AutoCAD has it** (#64) -- alignment paths at the polar angles from
  the last point, a cursor that locks onto a path once it is within the aperture of it,
  the dashed tracking line and the tooltip (`Polar: 12.3456 < 45°`). The increment angle
  is chosen from the POLAR dropdown (90, 45, 30, 22.5, 18, 15, 10, 5) or `POLARANG`;
  `POLARADDANG` adds single angles and `POLARMODE` measures from the last segment.
  **PolarSnap** (`SNAPTYPE` 1, `POLARDIST`) steps along the path.
- **Object snap tracking** (F11, OTRACK) -- a point the cursor rests on is acquired; the
  cursor then tracks along the paths through it, onto the crossing of two of them or of
  one and a polar path. `AUTOSNAP` switches the two tracking modes.
- **Temporary override keys** while a command asks for a point: Shift (ORTHO the other
  way), Shift + A (OSNAP), Shift + X (POLAR), Shift + Q (OTRACK), Shift + D (nothing at
  all), Shift + E / V / C (Endpoint, Midpoint or Center alone). `TEMPOVERRIDES` 0
  switches them off.

- **TEXT as AutoCAD has it** (#42) -- `[Justify/Style]` with the fifteen justifications
  (`Left Center Right Align Middle Fit TL TC TR ML MC MR BL BC BR`; Align and Fit run the
  text between two points), the height and the rotation given as a value or a point and
  remembered, and line after line of text: each Enter starts the next line under the
  last, an empty line ends the command. The line is drawn on the canvas where it will
  stand while it is typed.
- **JUSTIFYTEXT** (the justification changes, the text stays where it is), **SCALETEXT**
  (each text about a point of its own; a height, a factor or Reference, or another
  text's height) and **TXT2MTXT**.
- **TEXTEDIT** asks for one object after another (`[Undo/Mode]`, `TEXTEDITMODE`).
- **DIMLINEAR as AutoCAD has it** (#56) -- `<select object>` (Enter, then a line, a
  polyline segment, an arc or a circle) and `[Mtext/Text/Angle/Horizontal/Vertical/Rotated]`;
  DIMALIGNED takes `<select object>` and `[Mtext/Text/Angle]`. The text typed at Text
  stands for the value, `<>` for the measurement.
- **`--check` checks the text** (#80) -- besides reading the drawing, it reports every
  text whose letters overlap another text's, reach outside the frame (`--window`, or
  the drawing's largest rectangle) or use a character the font has no glyph for, and
  with `--lines` every text a line crosses. `--json` prints the report for a program;
  the exit code is 4 when there is a problem. See docs/CLI.md.
- The stroke font draws the em dash, the multiplication sign, omega (and the ohm sign)
  and the micro sign (and mu): "40 × 30 mm", "750 Ω", "35 µm" (#79).

### Fixed
- DIMLINEAR chose horizontal or vertical from the axis its two points differ most on, so
  a vertical dimension of a mostly horizontal pair could not be drawn (#56). As in
  AutoCAD, where the dimension line is placed decides now -- above or below the points
  it measures across, beside them up -- and the preview turns with the cursor. The angle
  is kept with the dimension (`.musa`, DXF 50), so grip edits and DIMCONTINUE /
  DIMBASELINE keep it. A horizontal dimension of a mostly vertical pair in a DXF from
  elsewhere was read as vertical.
- A centred or right-justified text was bounded, picked and given its edit box as if it
  were left-justified, and one read from a DXF was placed on its first point instead of
  the point it is justified on. Drawing, bounds, picking, grips and the edit box share
  one layout now, and DXF text carries 72 / 73, 11 / 21 and 41 both ways.
- OFFSET with Layer = Source put the offset of a line, an arc or a circle on the current
  layer with the current colour and linetype. It takes the source's properties, linetype
  scale included.
- The Hatch Editor and Text Editor tabs showed nothing of the selected object: the
  pattern read SOLID and Scale, Angle and Height stayed empty. They show its pattern,
  scale, angle, font and height, and are blank when the selection's values differ.
- The Properties palette wrote lengths and coordinates past four digits with an exponent
  (a 10850 mm wall read `1.085e+04`) and dropped their decimals. They read in full now.
- The POLAR button's dropdown arrow was drawn as a black box while POLAR was off.
- The tracking tooltip was drawn with the digit font of the Dynamic Input fields and read
  `P 2600.0000 90` instead of `Polar: 2600.0000 < 90°`.

### Changed
- The frame-rate readout in the drawing area and the frame rate and build date in the title
  bar are off by default; **Options > Show performance overlay** brings them back. The
  build date stays in **About**.
- POLAR no longer turns every cursor position to the nearest 45 degrees: it takes the
  cursor only near a polar path, and the increment starts at 90 degrees, AutoCAD's
  default. Pick 45 from the POLAR dropdown for the angles it had.
- The **Centroid** object snap takes AutoCAD's name and keyword: **Geometric Center**,
  `GCEN`.
- The running object snaps start as AutoCAD's default set -- Endpoint, Center,
  Intersection and Extension (OSMODE 4133). The others are a click away in the OSNAP
  dropdown, or typed for one pick.
- A polyline's arc segments snap at the middle of the arc (it was the middle of the
  chord) and at the arc's centre.
- **DONUT** makes what AutoCAD makes: a closed polyline of two half-circle arcs on the
  mean diameter, as wide as the ring is thick (it was a two-loop solid hatch). It can be
  edited, offset and exploded as a polyline, and FILLMODE applies to it.
- The native format is version 38: `POLYLINE` records may end with `W` and two widths per
  vertex and `Z` with the elevation and thickness; a `FILLMODE` record; `TEXT` and
  `ATTDEF` records may carry the alignment point and the text's own width factor. Older
  files open unchanged.
- TEXT no longer ends after one line (scripts and macros add an empty line to end it).
- DXF: LWPOLYLINE writes and reads the constant width (43), the per-vertex widths (40 /
  41), the elevation (38) and the thickness (39); the legacy POLYLINE / VERTEX form's
  widths are read as well.

### Fixed
- The ODA File Converter download (DWG Setup) failed with "server replied: Not Found": the
  Open Design Alliance renamed its files (no release number in the name any more) and the
  built-in name pointed at the old one. The page's names are read either way now, the link
  without a number is followed to the current build, and its version is taken from where
  the file server sends the download.

## 0.5.0 - 2026-09-26

### Fixed
- FILLET and CHAMFER rebuilt the trimmed lines, the arc, the bevel and the polyline without
  their properties, so the results landed on the current layer with default colour and
  linetype. They keep the objects' layer, colour, linetype and linetype scale; the arc or
  bevel takes the objects' layer when both share it.
- Windows: the engine's command dispatcher no longer compiled with MSVC once its `else if`
  chain passed the compiler's block-nesting limit (C1061); the branches are independent
  now, with the same behaviour. The portable `parse_double` fallback (the shim self-test)
  uses the MSVC CRT's spelling of the per-call locale API, so the `dev` preset builds
  there too.

### Added
- **AutoCAD's selection conventions** (#46) -- every edit command now starts at `Select
  objects:` when nothing is selected (MOVE, COPY, MIRROR, ROTATE, SCALE, ALIGN, ARRAY,
  ERASE, EXPLODE, STRETCH, MATCHPROP's destinations, ISOLATEOBJECTS, SELECT), gathering
  picks, windows, crossings and lassos until Enter or right-click, with the keywords
  Window, Crossing, BOX, ALL, Fence, WPolygon, CPolygon, Group, Last, Previous, Add,
  Remove, Multiple, Undo, AUto and SIngle and AutoCAD's "N found" echoes. Shift + click
  takes a selected object out (PICKADD), an empty click starts a click-click box
  (PICKAUTO), press-and-drag draws a lasso with Space cycling window / crossing / fence
  (PICKDRAG), the box or lasso tints blue or green and highlights what it would select
  (SELECTIONPREVIEW), a pick over several objects offers the list (SELECTIONCYCLING,
  Ctrl+W), Ctrl+A selects all. New commands: SELECT, SELECTSIMILAR (with
  SELECTSIMILARMODE), QSELECT and FILTER (the Quick Select dialog), ISOLATEOBJECTS /
  HIDEOBJECTS / UNISOLATEOBJECTS, and PICKBOX, PICKFIRST, PICKADD, PICKAUTO, PICKDRAG,
  HIGHLIGHT, SELECTIONPREVIEW, SELECTIONCYCLING as system variables. The idle right-click
  menu carries Repeat, Recent Input, Clipboard, Isolate, Erase, Select Similar, Quick
  Select, Deselect All, Undo / Redo and Properties. The ribbon's Move, Copy, Mirror,
  Rotate, Scale and Array stay enabled, as AutoCAD's do.
- **ERASE, OOPS, per-pick Undo, MATCHPROP by window, GROUP options, PURGE** (#53) -- ERASE
  gathers a set (a pre-selection goes at once) and OOPS brings the last erased set back
  without undoing later work; TRIM and EXTEND make every pick its own undo step and offer
  Undo; MATCHPROP's destinations are a `Select objects:` step (a window or crossing
  applies to everything it catches), with AutoCAD's full Property Settings list and the
  `Current active settings:` line; GROUP `?` lists the groups, `-GROUP` has ?, Order, Add,
  Remove, Explode, REName, Selectable and Create, GROUPEDIT edits by pick or name, Ctrl+H
  toggles group selection and PICKSTYLE takes 0 to 3; PURGE is a dialog (categories,
  names, Confirm each item, zero-length geometry, empty text objects) and -PURGE asks the
  type, the names with wild cards and Verify each name.
- **UNITS as a dialog, -UNITS as AutoCAD's numbered prompts, INSUNITS, the linetype-scale
  variables** (#67) -- the Drawing Units dialog with length and angle types and
  precisions, clockwise, the insertion scale, a sample output and Direction Control;
  -UNITS prints the format tables and asks `Enter choice, 1 to 5 <2>:` and the fraction
  denominator; INSUNITS is kept with the drawing (native and DXF); LTSCALE shows the
  current value as its default and says `Regenerating model.`; CELTSCALE sets the linetype
  scale new objects take; PSLTSCALE scales linetypes seen through layout viewports by the
  viewport scale; MSLTSCALE is recorded.
- **The inquiry commands as AutoCAD has them** (#63) -- AREA asks `Specify first corner
  point or [Object/Add area/Subtract area] <Object>:` and measures picked points with
  `[Arc/Length/Undo]` and `[Arc/Length/Undo/Total] <Total>` (arcs tangent to the last
  segment or through a Second pt), keeps Add and Subtract running totals, and reports an
  open object as if closed plus its length; DIST has `[Multiple points]` with a running
  total and the full readout (Angle from XY Plane, Delta Z); ID reports Z; LIST takes a
  `Select objects:` set and prints AutoCAD's block per object (kind, layer, space, handle,
  colour, linetype, lineweight, the geometry, area and perimeter). New: MEASUREGEOM (MEA)
  with Distance, Radius, Angle, ARea, Volume, Quick and Mode; MASSPROP (area, perimeter,
  bounding box, centroid, moments and product of inertia, radii of gyration, principal
  moments); TIME with the creation, last-saved and total editing times kept in the drawing
  and the elapsed timer's Display/ON/OFF/Reset; STATUS; CAL and QUICKCALC (a calculator
  palette with a number pad, scientific functions, unit conversion and paste to the command
  line); DWGPROPS (General, Summary, Statistics, Custom), saved with the drawing.
- Enter at a point prompt that has no default asks again instead of complaining (#38).
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
