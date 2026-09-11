# Musa CAD — Commands & Shortcuts (living roadmap)

This is the living specification of Musa CAD's AutoCAD-style commands. Each row
has a **Status**: `Implemented`, or `Planned (Phase N)`. Every future phase
implements a slice and flips rows to `Implemented`. Phase numbers beyond the
current one are a tentative roadmap and may shift.

Conventions: type the **Alias** or full name at the command line (autocomplete
suggests both); aliases are case-insensitive.

## Command descriptions (ribbon tooltips)

Every command carries a one-line **description** and an **icon** in its registry row
(`CommandRegistry::make_default`, the single source of truth). The ribbon reads these to
build each button's icon and its hover tooltip (`NAME (ALIAS) — description`); the same
descriptions are available for future help / command-search. Catalog of the 38 registered
commands (Ribbon Phase A):

| Command (alias) | Description |
|---|---|
| LINE (L) | Create a series of straight-line segments. |
| CIRCLE (C) | Draw a circle from a center point and a radius or diameter. |
| PLINE (PL) | Draw a connected sequence of line and arc segments as one object. |
| ARC (A) | Draw a circular arc through three points. |
| RECTANGLE (REC) | Draw a rectangle from two opposite corners. |
| ERASE (E) | Delete selected objects from the drawing. |
| UNDO (U) | Reverse the most recent action. |
| ZOOM (Z) | Zoom in or out to change the view magnification. |
| MOVE (M) | Move selected objects a specified distance and direction. |
| COPY (CO) | Duplicate selected objects at a specified offset. |
| MIRROR (MI) | Create a mirror-image copy of objects across an axis. |
| OFFSET (O) | Create a parallel copy of a curve at a specified distance. |
| TRIM (TR) | Trim objects to meet the edges of other objects. |
| JOIN (J) | Join collinear or connected objects into a single object. |
| ROTATE (RO) | Rotate selected objects around a base point. |
| SCALE (SC) | Resize selected objects uniformly about a base point. |
| ARRAY (AR), -ARRAY | Create a rectangular, path or polar pattern of copies. |
| ARRAYRECT | Create a rectangular pattern of copies in rows and columns. |
| ARRAYPOLAR | Create a circular pattern of copies about a centre point. |
| ARRAYPATH | Distribute copies evenly along a path curve. |
| POLYGON (POL) | Draw a regular polygon by centre or by one edge. |
| REVCLOUD | Draw a revision cloud, or turn an object into one. |
| EXPLODE (X) | Break compound objects into their components. |
| PURGE (PU) | Remove unused layers, dimension styles, table styles, blocks, image definitions or empty groups (type prompt, All by default). |
| OSNAP (OS, DDOSNAP) / -OSNAP | The running object-snap settings dialog; -OSNAP takes a mode list (END, MID, CEN, NOD, QUA, INT, PER, TAN, NEA, INS, APP, PAR, NONE, ALL). |
| WIPEOUT | Mask polygon from points or a closed polyline ([Polyline], with optional erase); [Frames] ON/OFF shows or hides the boundaries. Hides lines, curves and hatches beneath it; text stays visible. |
| FIELD | Text carrying %<Date>%, %<Time>%, %<Filename>% or %<Login>%, expanded at layout and refreshed on every regen. |
| PEDIT (PE) | Edit a polyline: Close/Open, Join, Edit vertex (Insert/Delete/Move), Spline (a fit spline through the vertices), Decurve, Reverse, Undo; a picked line or arc is converted first. |
| BLOCK (B, -BLOCK) | Make the selection a block definition (name, base point, select objects); the originals are replaced by one insert in place. |
| INSERT (I, -INSERT) | Insert a block by name: insertion point, X/Y scale, rotation (? lists blocks). |
| ATTDEF (ATT, -ATTDEF) | Define a block attribute: modes (Invisible/Constant/Verify/Preset), tag, prompt, default value, then the text placement. Shows its tag until BLOCK folds it into a definition. |
| ATTDISP | Attribute visibility: Normal (each attribute's own Invisible mode), ON or OFF for all. |
| ATTEDIT (-ATTEDIT) | Change one attribute value (by tag, or all) on a block reference. |
| XREF (XR, -XREF) | External references: Attach a .musa or .dxf as a block named after the file (its own blocks come along as name\|block), placed at a point, scale and rotation; Detach erases its references and definition; Reload re-reads the file; ? lists them. Xrefs are re-read whenever the drawing opens. |
| IMAGEATTACH (IAT) | Attach a raster image: file (or the file dialog), keep a copy inside the drawing or reference it from the drawing's folder, insertion point, scale (1 pixel = 1 unit at 1), rotation. |
| IMAGECLIP (ICL) | Clip an image to a rectangular or polygonal boundary (Close with Enter); Delete, ON, OFF drop or keep the boundary. |
| IMAGEFRAME | Image frames: 0 hidden, 1 shown and plotted, 2 shown on screen only. |
| LAYOUT (LO) | Paper-space sheets: New, Copy (objects included), Delete (an empty one), Rename, Set, ?. The tab strip does the same by click and right-click. |
| MODEL / PSPACE (PS) | Switch to model space / to paper space (the first layout). |
| MVIEW (MV) | A viewport on the layout showing model space: two corners, or Fit (the sheet inside a 10 mm margin), fitted to the whole model; ON / OFF, Scale (paper mm per model unit), Center (the model point at the viewport's centre). Corner grips resize, the centre grip moves. |
| MSPACE (MS) | Edit model space through a viewport: pick one (or double-click inside it). The model fills the window at the viewport's scale; draw and edit as in model space; PSPACE (or a double-click on nothing) returns to the sheet, and the view you left becomes the viewport's view, so ZOOM and PAN inside set its scale and centre. |
| REFEDIT | Edit a block definition in place: the picked reference's members become ordinary objects (the working set) to edit with any command. |
| REFSET | Add objects to, or remove them from, the working set of the reference being edited. |
| REFCLOSE | Save the working set back into the block definition (every reference updates) or discard the changes. |
| WBLOCK (W) | Write a block (base point at the origin) or the whole drawing to a .musa file. |
| REGEN (RE) | Rebuild and redraw the scene. |
| STYLE (ST, -STYLE) | Named text styles: font, fixed height, width factor, obliquing angle; TEXT uses the current style and the palette's Style dropdown changes it. |
| UNITS (UN, -UNITS) | Length/angle display format and precision, base angle, clockwise; used by the readout and inquiry commands. |
| AUDIT | Validate layer/style/font/block/image references and entity shape; Yes fixes them. |
| VIEW (V) | Save, Restore, Delete and list (?) named views; Window saves a framed view. |
| GROUP (G) | Make the selection a named group (Name/Description); picking a member selects the group. |
| UNGROUP | Dissolve a group by pick or by name. |
| PICKSTYLE | 0/1: whether picking a member selects its whole group. |
| ALIGN (AL) | Move, rotate and optionally scale a selection onto two destination points. |
| LENGTHEN (LEN) | Change the length of a line or arc. |
| BREAK (BR) | Break a curve between two points. |
| BREAKATPOINT | Split a curve at one point, leaving no gap. |
| ELLIPSE (EL) | Draw an ellipse or elliptical arc (axis endpoints, Center, Rotation, Arc). |
| SPLINE (SPL) | Draw a spline through fit points (Method Fit, Knots Chord/Square root/Uniform) or by control vertices (Method CV, Degree). Undo, Close. |
| DONUT (DO) | Filled ring or disc from inside/outside diameters (drawn as a SOLID hatch with two loops). |
| XLINE (XL) | Draw an infinite construction line. |
| RAY | Draw a semi-infinite construction line. |
| POINT (PO) | Place point objects; Esc ends. |
| DIVIDE (DIV) | Mark a curve into a number of equal segments with points. |
| MEASURE (ME) | Mark a curve at set intervals with points. |
| EXTEND (EX) | Extend objects to meet the edges of other objects. |
| FILLET (F) | Round corners between two intersecting lines, arcs, or polylines. |
| CHAMFER (CHA) | Bevel corners between two intersecting lines. |
| MATCHPROP (MA) | Copy properties from a source object to one or more target objects. |
| HATCH (H) | Fill an enclosed area with a pattern or solid color; [Gradient] blends the entity colour into a second colour along an angle. |
| TEXT (DT) | Create a single-line text object. |
| DIMLINEAR (DLI) | Create a horizontal or vertical linear dimension. |
| DIMALIGNED (DAL) | Create a dimension aligned with two points. |
| DIMRADIUS (DRA) | Create a radius dimension for a circle or arc. |
| DIMDIAMETER (DDI) | Create a diameter dimension for a circle or arc. |
| DIMANGULAR (DAN) | Create an angular dimension between two lines. |
| DIMORDINATE (DOR) | Ordinate (X or Y datum) dimension with a leader; Xdatum/Ydatum or automatic from the leader direction. |
| DIMJOGGED (DJO) | Jogged radius dimension: arc or circle, centre location override, dimension line location, jog location. |
| DIMARC (DAR) | Arc length dimension of an arc or a polyline arc segment. |
| DIM | Create a dimension suited to the selected object. |
| LEADER | Draw a leader line with an arrowhead and annotation. |
| MTEXT (MT) | Create a multiline (paragraph) text object. |
| QLEADER (LE) | Draw a quick leader with an arrowhead and annotation. |
| TEXTEDIT (ED) | Edit the contents of an existing text object. |
| PROPERTIES (PR) | Open the Properties palette to view and edit object properties. |
| DWGIN | Import geometry from a DWG file. |
| DWGOUT | Export the drawing to a DWG file. |
| PLOT | Plot or print the drawing to paper or PDF. |
| LTSCALE (LTS) | Set the global linetype scale factor. |

## Draw

| Command | Alias | Status |
|---|---|---|
| LINE | L | Implemented |
| PLINE (polyline) | PL | Implemented |
| CIRCLE | C | Implemented |
| ARC (3-point) | A | Implemented |
| RECTANGLE | REC | Implemented |
| RECTANGLE options: **Dimensions** (`D` → length → width → quadrant-flip placement click), **Area** (`A` → area → `[Length/Width]` → side → placement), **Rotation** (`R` → angle) | typed mid-command | Implemented (option keywords, same state machine as CIRCLE `[Diameter]`) |
| RECTANGLE first-corner options: Chamfer / Elevation / Fillet / Thickness / Width | C/E/F/T/W | Planned |
| SPLINE | SPL | Planned (Phase 7) |
| ELLIPSE | EL | Planned (Phase 7) |
| POLYGON | POL | Planned (Phase 7) |
| POINT | PO | Planned (Phase 7) |
| XLINE / RAY | XL | Planned (Phase 7) |
| HATCH / BHATCH — **SOLID fill** of a region (Part A). Two boundary modes: **pick an internal point** (the engine traces the enclosing boundary from surrounding geometry — lines, arcs, circles, polylines — building a **planar arrangement** so a partitioning line correctly splits the region; closed entities inside become **islands/holes**) or **pre-select** closed polylines (noun-verb). "Valid hatch boundary not found." when no closed boundary encloses the pick. Fill is **derived, not baked** (rendered via the fill pipeline, so it plots as PDF vectors). Pickable (point-in-region, islands respected), PR-editable (Pattern/Scale/Angle/Origin), MATCHPROP-matchable, native + DXF round-trip. Selected hatch shows a **highlight tint over the fill + a grip at every boundary vertex** (drag to reshape) | H / BHATCH | Implemented (Part A: SOLID; line patterns = Part B) |
| HATCH **line patterns** (Part B) — `.PAT` parser + a built-in stock library (ANSI31–ANSI38, NET/NET3/GRID, BRICK, BOX, HEX/HONEY, ANGLE, DOTS, CROSS, SQUARE, TRIANG, GRASS, EARTH, STEEL, CONC, INSUL, …; authored from the public .PAT format, **not** copied from acad.pat — load that file for the vendor set). Line families are **generated + clipped to the boundary at render time** (derived-not-baked, even-odd so islands carve out) and plot as vectors. Choose via the command's `[Pattern/Scale/Angle]` options or the PR. SOLID stays the special fill name — one render path, patterns are not a fork | H ▸ Pattern | Implemented (Part B) |
| HATCH GRADIENT fills (entity colour to a second colour along an angle; 24 flat bands, so it plots as vectors; DXF gradient block both ways) | H ▸ Gradient | Implemented |
| WIPEOUT (points, or [Polyline] from a closed polyline with optional erase; [Frames] ON/OFF = WIPEOUTFRAME, saved with the drawing). Masks lines, curves and hatches beneath it; text always shows through (DRAWORDER is not modelled). DXF: a colour-7 solid | WIPEOUT | Implemented |
| FIELD (Date, Time, Filename, Login) as `%<Date>%`-style codes in any text, expanded at layout and refreshed on every regen | FIELD | Implemented |

## Modify

| Command | Alias | Status |
|---|---|---|
| ERASE (Last / All / pick) | E | Implemented |
| UNDO | U | Implemented |
| REDO | — (Ctrl+Y) | Implemented |
| MOVE | M | Implemented |
| COPY | CO / CP | Implemented |
| MIRROR | MI | Implemented |
| OFFSET (line/circle/arc) | O | Implemented |
| OFFSET (polyline, incl. closed rectangles + bulged/filleted corners) — each segment offset (lines parallel, arcs concentric with the bulge preserved) and **corners re-mitered** as the intersection of adjacent offset curves (line/line, line/arc, arc/arc via the shared line_line / line_circle / circle_circle primitives), so edges stay at distance d with clean corners (no trapezoid). Over-large offsets that would fold the shape fail gracefully ("Offset distance too large for this polyline.") leaving the geometry unchanged | O | Implemented |
| ROTATE | RO | Implemented |
| SCALE | SC | Implemented |
| ARRAY (asks the type: Rectangular / PAth / POlar) | AR, -ARRAY | Implemented |
| ARRAYRECT (rows, columns, spacings, angle of axes) | ARRAYRECT | Implemented |
| ARRAYPOLAR (centre, count, fill angle, rotate items) | ARRAYPOLAR | Implemented |
| ARRAYPATH (Divide / Measure, align to path) | ARRAYPATH | Implemented |
| ARRAYEDIT / ARRAYCLOSE | - | Not applicable (arrays are non-associative -- see below) |
| POLYGON (centre: Inscribed/Circumscribed; Edge) | POL | Implemented |
| RECTANGLE [Chamfer] / [Fillet] corner options (session defaults, last set wins) | REC | Implemented |
| RECTANGLE [Width] / [Elevation] / [Thickness] | REC | Not offered (no polyline width; 3D) |
| REVCLOUD (Arc length, Object + Reverse direction, Rectangular, Polygonal, Freehand as a clicked path) | REVCLOUD | Implemented (Normal style; Modify not offered) |
| EXPLODE (polyline -> lines/arcs; block one level; dimension/leader -> lines, solids, text; hatch -> lines or boundary; MTEXT -> TEXT per line; table -> lines + text) | X | Implemented |
| PURGE (unused layers) | PU | Implemented |
| PURGE (dimstyles, text styles, block definitions) | PU | Planned |
| ALIGN (2 point pairs, optional uniform scale) | AL | Implemented |
| LENGTHEN (DElta / Percent / Total; lines + arcs) | LEN | Implemented |
| BREAK (line, arc, circle, open + closed polyline) | BR | Implemented |
| BREAKATPOINT (split, no gap) | BREAKATPOINT | Implemented |
| ELLIPSE (axis-end / Center / Rotation / Arc by angle, parameter, included) | EL | Implemented |
| SPLINE (Fit with Knots; CV with Degree; Undo; Close) | SPL | Implemented (Tangency, fit tolerance, Object deferred) |
| XLINE (Hor / Ver / Ang / Bisect / two-point) | XL | Implemented (Offset deferred) |
| DONUT | DO | Implemented (as a SOLID two-loop hatch; no polyline width) |
| VIEW Save/Restore/Delete/?/Window | V | Implemented (Orthographic/Ucs are 3D-only) |
| GROUP / UNGROUP / PICKSTYLE | G | Implemented (group membership persists in .musa; not on the undo stack) |
| UNITS (Scientific/Decimal/Engineering/Architectural/Fractional; degrees/DMS/grads/radians/surveyor) | UN | Implemented (dimension text keeps the dimstyle precision, as DIMLUNIT does) |
| STYLE (text-style table; width factor and oblique applied to TEXT; DXF STYLE table both ways) | ST | Implemented (backwards/upside-down/vertical deferred; MTEXT keeps fonts directly) |
| BLOCK / INSERT / WBLOCK (lines, circles, arcs, polylines, text, mtext, nested inserts) | B / I / W | Implemented |
| Block attributes: ATTDEF entities become attributes on BLOCK; INSERT prompts for each value (Constant/Preset skipped); ATTDISP; ATTEDIT; EXPLODE gives the definitions back; native v28 and DXF ATTDEF / INSERT+ATTRIB+SEQEND both ways | ATT / ATTDISP / ATTEDIT | Implemented (the dialog forms EATTEDIT/BATTMAN and multi-line attributes are not) |
| REFEDIT / REFSET / REFCLOSE: in-place block editing through a working set, mapped back into the definition's frame on save (needs a positive, uniform scale); Discard restores the reference; undo takes back the save as one step | REFEDIT | Implemented (no fading of the rest of the drawing during the edit) |
| Layouts / paper space: a layout table with page setups, per-entity space (model or a layout), only the active space drawn / picked / edited, the sheet drawn under a layout, layout tabs (click, right-click New/Rename/Delete), PLOT of a layout = its sheet at 1:1, native v30, DXF paper-space flag (67) both ways | LAYOUT / MODEL / PSPACE | Implemented |
| Viewports (MVIEW): a paper-space entity showing model space at a scale, derived at snapshot time (model geometry mapped into the sheet and clipped to the frame; text, hatches, wipeouts and whole images included), plotted with the sheet, grips, native v31 and DXF VIEWPORT both ways | MVIEW | Implemented (per-viewport layer freezing is not) |
| MSPACE / PSPACE through a viewport: double-click in / out, the camera hand-off keeps the viewport's on-screen size, the view left on PSPACE becomes the viewport's view | MSPACE | Implemented (the sheet is not shown around the model while inside; a tab switch drops the edit without a write-back) |
| PEDIT (Close/Open/Join/Edit vertex/Spline/Decurve/Reverse/Undo) | PE | Implemented (Width, Fit, Ltype gen, Multiple deferred; Spline yields a SPLINE entity) |
| Object snaps: Insertion, Apparent intersection, Parallel; OSNAP settings dialog; -OSNAP | OS | Implemented (Apparent intersection and Parallel are opt-in, as in AutoCAD) |
| ROTATE/SCALE [Copy]/[Reference]; Rotate/Scale value dialogs with live ghost | RO / SC | Implemented |
| Properties palette: feature control frame cells and datum letter editable | PR | Implemented |
| Properties palette: editable Start/End/Center X-Y, Radius, Position X-Y | PR | Implemented |
| DXF TOLERANCE: feature control frames both ways (cells joined by %%v, GD&T symbols as {\\Fgdt;x}); datum symbols written as one-cell frames | DXFOUT/DXFIN | Implemented (a datum's leader triangle has no DXF form) |
| AUDIT | AUDIT | Implemented |
| PURGE by type (blocks, dimstyles, table styles, image defs, groups, layers) | PU | Implemented (text styles: not a table yet, see #29) |
| RAY (start + through points) | RAY | Implemented |
| POINT | PO | Implemented |
| DIVIDE (n equal segments) | DIV | Implemented |
| MEASURE (fixed interval) | ME | Implemented |
| TRIM a line (cut by line/circle/arc edges) | TR | Implemented |
| TRIM an arc/circle *entity* | TR | Implemented (a trimmed circle becomes an arc) |
| TRIM a polyline *entity* (open or closed, arc segments cut exactly) | TR | Implemented |
| EXTEND a line (to line/circle/arc boundary) | EX | Implemented |
| EXTEND an arc *entity* | EX | Implemented |
| EXTEND an open polyline *entity* (straight end segment; polylines also act as boundaries) | EX | Implemented (an arc end segment is refused) |
| FILLET (line/line; radius 0 or tangent arc) | F | Implemented |
| FILLET (polyline corner → a true arc segment / bulge, dimensionable) | F | Implemented (incl. RECTANGLE corners — a rectangle IS a closed polyline; verified end-to-end through the full F-command path, including the closing-edge wrap corner) |
| Polyline arc segments (per-vertex bulge, AutoCAD LWPOLYLINE) | — | Implemented |
| FILLET line/arc, line/circle, arc/arc, arc/circle, circle/circle (radius > 0; nearest to the picks) | F | Implemented |
| CHAMFER (line/line; Distance or Angle method, 45° default) | CHA | Implemented |
| CHAMFER (polyline corner) | CHA | Implemented |
| JOIN — **select** lines/arcs/open polylines (any way), then JOIN merges every connected chain among them into ONE polyline each (arcs become bulged segments), inheriting the source's layer/properties; a chain whose ends meet becomes a **closed** polyline (which then OFFSETs uniformly). The merged polyline is a single entity — moving it or a grip keeps it connected. With nothing pre-selected, JOIN falls back to picking a source + targets. One undo group | J | Implemented |
| ARRAY dialog (interactive grid/preview) | AR | Planned (Phase 13) |
| **STRETCH** — AutoCAD's flow, prompt for prompt: `Select objects:` (pick, window or crossing drags accumulate with "N found"; **Enter or right-click** ends it; a pre-selected set skips it) → `Specify base point or [Displacement] <Displacement>:` → `Specify second point or <use first point as displacement>:`, with the selection **stretched live under the cursor** (ortho-aware). AutoCAD's rule decides what moves: an object *crossed* by a crossing window has only the vertices inside it moved; one fully enclosed, or selected by a pick or an ordinary window, moves whole; a line that merely passes through the window is left alone. An arc endpoint moves with the arc's height above its chord preserved; circles and single-point kinds (text, insert, image, table, GD&T) move if their centre/anchor is caught and are never deformed; a dimension whose def points move **re-measures**. One undo group; reports what it did or why nothing moved | S | Implemented (issue #24) |
| **DIST (DI)** — distance, angle and delta X/Y between two points. Answered from the picked points alone, so it never reaches the store | DI | Implemented (issue #30) |
| **ID** — the coordinates of a point | ID | Implemented (issue #30) |
| **AREA (AA)** — area and perimeter of a closed object (exact for circles, shoelace over the SAME tessellation the renderer draws for everything else). An OPEN object reports its length and says it has no area, rather than the area of the polygon you would get by closing it | AA | Implemented (issue #30) |
| **LIST (LI)** — an object's type, layer and defining parameters. A dimension reports its MEASURED value, which by construction cannot have been authored | LI | Implemented (issue #30) |
| EXPLODE | X | Planned (Phase 13) |
| JOIN | J | Planned (Phase 13) |

### The ARRAY family, and why ARRAYEDIT is absent

AutoCAD ships seven array commands. Four of them create an array and are implemented
here in full: `ARRAY`/`-ARRAY` (which ask for the type), `ARRAYRECT`, `ARRAYPOLAR` and
`ARRAYPATH`. As in AutoCAD, `PA` selects Path and `PO` selects Polar; a bare `P` is
ambiguous and is refused rather than guessed at.

`ARRAY` keeps AutoCAD's legacy four rectangular prompts (rows, columns, row spacing,
column spacing) unchanged. The axis angle is offered by the modern `ARRAYRECT`, which
matches where AutoCAD puts it and leaves existing muscle memory alone. The angle
rotates the row/column **axes**, not the items: a rotated rectangular array is a skewed
lattice of upright copies.

`ARRAYPATH` supports both of AutoCAD's distribution methods:

- **Divide** -- spread N items over the whole path. On an open path the first and last
  items sit on the endpoints; on a closed path the items divide by N rather than N-1, so
  nothing is doubled at the seam.
- **Measure** -- place an item every given distance, optionally capped by an item count.
  Stations never run past the end of the path.

Aligned items turn to follow the path tangent, measured relative to the tangent at the
first station, so item 1 keeps the orientation you drew and the rest follow the curve.
The path curve is left in the drawing and may not itself be part of the selection.

The remaining two, `ARRAYEDIT` and `ARRAYCLOSE`, edit an **associative** array -- a
parametric entity that remembers its source objects and parameters so the pattern can be
re-driven later. Musa CAD's arrays are non-associative: they produce ordinary
independent copies, exactly as AutoCAD's own `-ARRAY` does. There is therefore no
association for `ARRAYEDIT` to reopen and no array-editing state for `ARRAYCLOSE` to
leave. Adding them means adding associative arrays first, which is a data-model change
(a new parametric entity kind), not a command.

## Annotate / Dimensions

| Command | Alias | Status |
|---|---|---|
| TEXT (single-line) | DT / TEXT | Implemented |
| **MTEXT (paragraph text; two-corner box → wraps within the width)** | MT / MTEXT / T | Implemented (multi-line + paragraph fields; inline per-char formatting Planned) |
| MTEXT fields: defined width, height, width factor, line spacing, attachment (TL..BR), rotation, ByLayer/override colour | — | Implemented (discrete, queryable — Properties palette ready) |
| MTEXT grips: insertion (move) + width (re-wraps live) | — | Implemented |
| **QLEADER (arrow → leader vertices → attached MTEXT label)** | LE / QLEADER / QL | Implemented (label is owned → moves with the leader) |
| QLEADER grips: arrow tip + each vertex + text position | — | Implemented |
| **Double-click a TEXT / MTEXT / QLEADER label → edit its content** (dark modal editor, pre-filled) | (double-click) | Implemented (Ph21) |
| **TEXTEDIT / DDEDIT** (pick text → type new content; scriptable path) | ED / TEXTEDIT / DDEDIT | Implemented (Ph21) |
| Text edit = one undo group, preserves layer/properties/position (not delete+recreate) | — | Implemented (Ph21) |
| **AutoCAD control codes in TEXT & MTEXT** — `%%d`→°, `%%p`→±, `%%c`→⌀, `%%b`→⌴ counterbore, `%%h`→↧ depth, `%%v`→⌵ countersink, `%%%`→literal %, `%%nnn`→char by code, `%%o`/`%%u`→overline/underline toggles, `\U+XXXX`→any Unicode code point (**universal**, not MTEXT-only). Codes are **stored raw and expanded at render time** (editing shows the raw codes; save/load + DXF round-trip them). Shared with the Leader/MLeader labels | (type in any text) | Implemented |
| **Drafting symbols in the stroke font** — hole callouts (⌴ counterbore/spotface, ↧ depth, ⌵ countersink), the 14 GD&T characteristics (straightness, flatness, circularity, cylindricity, profile of a line/surface, angularity, perpendicularity, parallelism, position, concentricity, symmetry, circular/total runout), the 7 material-condition modifiers (Ⓜ Ⓛ Ⓢ Ⓟ Ⓕ Ⓣ Ⓤ), plus □ square, conical taper and slope. Same 6×8 cell and **same monospace advance** as the letters, so layout/bounds/pick are unchanged; works in TEXT, MTEXT, LEADER and dimension text alike. Hand-authored, not traced from any font | `%%b`/`%%h`/`%%v` or `\U+XXXX` | Implemented (issue #9) |
| **Leader / MLeader label properties in PR + MATCHPROP** — selecting a Leader or MLeader shows a Text section (Contents, Height, Font; MLeader also Width factor / Line spacing / Attachment); MATCHPROP copies the label font/height across the whole text family (TEXT ↔ MTEXT ↔ Leader ↔ MLeader) | PR / MA | Implemented |
| **Leader / MLeader arrow properties in PR + MATCHPROP** — a Leader section with **Arrowhead** (type) + **Arrow size**, each ByStyle or a per-leader override (override-first-else-dimstyle, like dimension overrides); native round-trip; MATCHPROP copies the arrow leader↔leader | PR / MA | Implemented |
| **Leader / MLeader label Text color** — a per-leader override so the label colours independently of the leader line + arrow (General colour); ByStyle = the entity colour; native round-trip; MATCHPROP leader↔leader | PR ▸ Text color | Implemented |
| **Properties palette (PR): dockable, context-sensitive panel for the selection** | PR / PROPERTIES / PROPS / CH | Implemented (Ph22) |
| PR multiplicity: nothing / one / many-same / many-mixed, with **\*VARIES\*** where values differ; edits set all | — | Implemented (Ph22) |
| PR universal props: **Layer / Color / Linetype / Lineweight** (ByLayer or override) editable single + multi + mixed | — | Implemented (Ph22) |
| PR Geometry group (editable since issue #32): line length/ends, circle/arc center+radius, text position | — | Implemented (Ph22; numeric geometry editing Planned) |
| PR full **Text / MTEXT** group: contents, height, rotation, justify, width factor, line spacing, defined width, attachment, **font** | — | Implemented (Ph22; font dropdown real in Ph29) |
| **Font** dropdown (Standard stroke font + system TrueType/OpenType faces); switching re-renders the selected text as one undo group (varies/set-all) | PR Font | Implemented (Ph29) |
| Imported text fonts: TTF-by-name resolves to the installed face (filled glyphs); single-stroke SHX fonts (romans/simplex/isocp/txt…) render with the built-in single-stroke font (faithful match); missing → stroke fallback (true SHX binary parsing staged) | — | Implemented (Ph29) |
| PR deep **Dimension** group: per-dimension overrides — arrowhead type/size, dim-line & ext-line color, text height/color/placement, precision (each ByStyle or Overridden, with reset-to-style) | — | Implemented (Ph24) |
| PR **Dimension** text decoration — text prefix / suffix (raw, so `%%c` and `6X` work) and a tolerance mode (none / symmetric / limits / basic / reference) with upper & lower deviations. The measured VALUE stays computed from the def points and is never authorable; only the decoration around it is. **Not** MATCHPROP-matchable by design — a fit class is semantics about THIS feature, not presentation | — | Implemented (issue #7) |
| PR **Dimension** **Text override** — AutoCAD's `<>` field: `<>` expands to the measurement so the override tracks the geometry (`<> H7`, `2x<>`); an override without `<>` replaces the value, shown explicitly rather than silently. Supersedes prefix/suffix and the deviation-deriving tolerance modes; Basic/Reference still frame it. Feeds the ISO 129-1 fit like any other label | — | Implemented (issue #20) |
| **ISO 129-1 narrow-dimension fit** — when the value and/or the arrowheads do not fit between the extension lines, the value is placed outside past the extension line and the arrowheads flip to point inward from beyond it. Text and arrows are tested **independently** (so arrows can stay inside while the value moves out). The test measures the fully decorated, code-substituted label (widest line when Limits stacks two). PR row **Text fit** = ByStyle / Auto / Always inside / Always outside; MATCHPROP-matchable (placement is presentation). Default Auto at both style and override level, so existing drawings simply stop colliding. Linear/Aligned; radial forms deferred | PR ▸ Dimension ▸ Text fit | Implemented (issue #12) |
| **TOLERANCE / TOL** — a GD&T **feature control frame**: an ordered cell list (characteristic, tolerance value, up to three datum references), drawing its own borders and dividers with uniform cell height derived from the DIMSTYLE text height (ASME Y14.5 proportions). Cells are ordinary text, so `\\U+2316` and `%%c` reach the stroke font's GD&T glyphs. Selectable as a unit (click anywhere inside), grip-movable, PR-editable (text height / colours), MATCHPROP-matchable with dimensions, native round-trip. **DXF: not written — stated gap** | TOL | Implemented (issue #8) |
| **DATUM / DIMDATUM** — a datum feature symbol: boxed datum letter + leader + **filled** triangle at the feature. Shares DIMSTYLE/overrides with dimensions; the leader attaches to whichever box edge faces the feature. Grips on both the box and the tip | DATUM | Implemented (issue #8) |
| **TABLE / TB** — a real table entity: rows x columns with stored column widths and row heights, cell text (with the usual `%%`/`\\U+` codes), per-cell alignment and **merged cells**. Borders, grid and text placement are derived from a **TABLESTYLE** (title / header / data text heights, margin, lineweight, colours), so a style edit re-lays out every table. Selectable as a unit (a click in any cell), grips move it and resize columns, native round-trip. **DXF ACAD_TABLE: not written — stated gap**; cell editing and row/column insert are staged | TB | Implemented (issue #22) |
| **Raster IMAGE entity** — an image-definition table (parallel to layers/dimstyles/blocks, deduped) plus placements carrying insertion point, size, rotation and an optional clip rectangle. Payload is an external path relative to the drawing **or** base64 embedded in the `.musa`. Selectable (point-in-quad, clip respected), grip-movable/scalable, bounds-correct, native round-trip, and **plots** (rasterised at output resolution, drawn under the vector geometry). Decoding goes through the core `IImageDecoder` seam so core stays Qt-free | — (no command yet) | Implemented: entity, native + plot, viewport display (textures decoded on the render thread and cached per definition), IMAGEATTACH / IMAGECLIP (rectangular and polygonal) / IMAGEFRAME, 8 MB embed limit, DXF IMAGE / IMAGEDEF both ways (embedded images are written beside the DXF) |
| Per-dimension overrides: resolve override-first-else-style (the Ph12 pattern) in compute_dim_geometry; one undo group; native round-trip; DXF override-vs-style distinction is native-only (stated gap) | — | Implemented (Ph24) |
| PR numeric **geometry editing** — the Geometry group's Start/End, Center/Radius and Position fields edit the entity | — | Implemented (issue #32) |
| LEADER (simple arrow + line + single-line label, kept for compat) | LEADER | Implemented |
| **DIM (smart all-in-one; hover previews the type, dispatches by entity)** | DIM | Implemented (line/poly→linear, circle→diameter, arc→radius) |
| DIMLINEAR (two-point, or `[Object]` → select a line / polyline segment) | DLI | Implemented |
| DIMALIGNED (two-point, or `[Object]` → segment's true length) | DAL | Implemented |
| DIMRADIUS (**select a circle/arc**, or a filleted polyline arc segment → R) | DRA | Implemented |
| DIMDIAMETER (**select a circle/arc** → ⌀ from its own geometry) | DDI | Implemented |
| DIMANGULAR (**select two lines/edges** → angle from their directions) | DAN | Implemented |
| DIMORDINATE (X/Y datum, leader dogleg) | DOR | Implemented (Mtext/Text/Angle deferred) |
| DIMJOGGED (centre override + jog) | DJO | Implemented (exports to DXF as a radius dimension) |
| DIMARC (arc or polyline arc segment) | DAR | Implemented (Partial/Leader deferred; exports to DXF as an aligned dimension carrying the arc-length text) |
| Arrowheads: filled / open / tick / dot (solid filled geometry) | DIMSTYLE | Implemented |
| DIMSTYLE: text height / arrow type+size / precision / ext lines | Dim Style btn | Implemented (Standard editable; multi-style manager Planned) |
| DIMSTYLE per-element colours (dim / ext / text / arrow) + dim lineweight | Dim Style btn | Implemented |
| Object-aware dims capture **def points** at creation (no entity ref) | — | Implemented (deleting the source entity never dangles the dim) |
| Placement preview: the full dimension (with live value) rubber-bands to the cursor, commits on click | — | Implemented (all dim types + DIM; angular arc is fixed by its two lines) |
| Associativity: value recomputed from def points each rebuild | — | Implemented (moving the *referenced* entity does not auto-update) |
| **DIMCONTINUE (DCO)** — chain the next dimension from the previous one's SECOND extension line, on the same dimension line. **DIMBASELINE (DBA)** — stack from the previous FIRST extension line, offset perpendicular by the baseline spacing. Both are placement helpers over the existing Linear/Aligned types (no new DimType, no format change) and inherit the previous dimension's style + overrides. Each pick is its own undo group, and the chain follows undo -- it continues from whatever is now last | DCO / DBA | Implemented (issue #28) |
| MTEXT (multi-line) | MT | Planned |
| MLEADER (multi-segment) | MLD | Planned |
| STYLE (text style) | ST | Planned |

## View / Navigate

| Command | Alias | Status |
|---|---|---|
| ZOOM (scale factor) | Z | Implemented |
| ZOOM Extents / All | Z→E/A | Implemented |
| PAN (middle-drag) | P | Implemented (mouse) |
| Zoom about cursor (wheel) | — | Implemented (mouse) |
| Smooth curves at any zoom (adaptive tessellation; re-tessellate on zoom, not pan) | — | Implemented |
| REGEN | RE | Planned (Phase 9) |
| Named views (VIEW) | V | Planned (Phase 9) |

## Layers / Properties

| Command | Alias | Status |
|---|---|---|
| Layer Manager (create/delete/rename, on/freeze/lock, colour/linetype/lineweight, set current) | ribbon | Implemented |
| Current-layer dropdown (ribbon) | — | Implemented |
| ByLayer / per-entity override resolution | — | Implemented |
| Move selection to layer | Layer Mgr ▸ Assign | Implemented |
| Set selection colour override | ribbon ▸ Set Colour | Implemented |
| **MATCHPROP / MA** — pick a source object (**or run MA with an object already selected → it becomes the source**, noun-verb), then pick destination object(s) (or **[Settings]**); each destination immediately adopts the source's properties via the Ph22 SetProperty descriptor path (the SAME write path the PR palette uses — no MA-specific entity-write code). **Universal** properties (colour / layer / lineweight / linetype — **ByLayer state preserved**, not the resolved literal) copy across ANY source/target kinds; **family-scoped** (text: height/font/justify/width-factor; dimension: per-dim overrides) copy only within a shared family; non-applicable properties are silently skipped. A **paintbrush cursor** shows while picking; **each matched target is its own undo entry** (undo in reverse). **Settings** (type `S`) opens a dark modal listing the categories (all on by default; persisted in QSettings for the session) | MA / PAINTER | Implemented |
| Off / Frozen layers skip rendering | — | Implemented |
| Locked layers (drawn, not selectable/modifiable) | — | Implemented |
| Linetype property (Continuous/Dashed/Center/Hidden) | — | Implemented (model + round-trip + **dashed rendering**, Ph23) |
| **Linetype pattern rendering** (dashes drawn on lines, polylines, and curves by arc-length; ByLayer + override) | — | Implemented (Ph23) |
| **LTSCALE** (drawing linetype scale, **per document**; prompts `<current>`, rejects ≤0; re-dashes live; native + DXF `$LTSCALE` round-trip) | LTSCALE / LTS | Implemented (Ph23) |
| **CELTSCALE** (per-entity linetype scale; PR "Linetype scale" field on line/circle/arc/polyline; MATCHPROP-matchable; native + DXF code 48 round-trip). Effective dash scale = **LTSCALE × CELTSCALE** | PR ▸ Linetype scale | Implemented |
| **Dynamic Input (canvas-only by default)** — all command input is drawn ON the canvas at the crosshair: command entry + autocomplete, mid-command sub-prompts, and on-geometry dimension fields. **F12 toggles** canvas-only ⇄ the classic bottom command-line bar (F12 ON = canvas, bar hidden; F12 OFF = bar visible, no canvas DYN). State persists; the bar always returns on F12 / the status DYN toggle (no-stuck fallback) | F12 / status DYN toggle | Implemented (Ph25–27; state persisted) |
| **DYN canvas command entry** — start typing on an idle canvas and a command-entry box appears at the cursor with the registry autocomplete dropdown (Down/Up/Tab select, Enter runs, Esc clears); routed by the app-wide event filter; bounded glyph batches regardless of suggestion count | — | Implemented (Ph27) |
| **DYN canvas sub-prompts** — any mid-command value/keyword step (FILLET radius, CHAMFER distances, RECTANGLE Dimensions length/width, Area, Rotation, option keywords) renders as an at-cursor prompt cell; type → Enter commits/advances, Esc cancels. Same primitive as entry + dimension fields | — | Implemented (Ph27) |
| DYN live dimensional input: type an exact length/angle (line), radius (circle), width/height (rectangle) during the rubber-band; Tab between fields | — | Implemented (Ph25; honors ORTHO/POLAR/snap) |
| **DYN on-geometry value fields** — value boxes drawn ON the canvas (in the GL viewport overlay, NOT OS windows) anchored to the rubber-band geometry, so they are always glued to it and cannot drift on multi-monitor (length under one edge, width by the other for RECTANGLE; length + angle for LINE; radius for CIRCLE), nudged just outside the edge so they never overlap. Type WITHOUT a click (the viewport captures dimension keystrokes); **Tab/Shift-Tab** switch fields; **Enter/Space** commit; **Esc** cancels; the mouse still drives the rubber-band. A typed value locks that dimension while the cursor drives the other(s) | F12 | Implemented (RECTANGLE/LINE/CIRCLE; other commands staged) |
| **DYN command-control keys (Esc / Enter / Space)** — with DYN on, these always reach the command (never swallowed by the on-canvas fields). **Esc** always cancels the active command — even with a half-typed value — clearing the rubber-band and fields. **Enter** / **Space** are AutoCAD's two-step: a pending typed value commits (keep drawing), otherwise the step ends (Enter at a LINE next-point prompt ends LINE). Tab/Shift-Tab still cycle fields. One carve-out in the app-wide event filter; F12-OFF (classic bar) unchanged | Esc / Enter / Space | Implemented |
| Single input surface per step: every step has exactly ONE place to type — the on-canvas entry (idle), sub-prompt cell (mid-command scalar/keyword), or dimension fields (rubber-band). The legacy cursor box is retired. State lives in the `CommandProcessor` (prompt + history), so hiding the bottom bar loses nothing; everything submits through the same pipeline (`compose_dyn_submit` / `submit_line`) | — | Implemented |
| **DYN autocomplete**: the Ph6 command-suggestion dropdown anchored at the cursor entry box (one suggestion source: the registry) | — | Implemented (Ph26–27) |
| Draw/transform ribbon buttons **start the interactive command** (pick on screen, like typing — never a fixed-position dialog): Line/Circle/Arc/Rectangle/Rotate/Scale | ribbon | Implemented (AutoCAD model) |
| **Starting a new command cancels the one in progress** — clicking a ribbon command (or otherwise dispatching a new command) while one is active cleanly cancels the current command first (its rubber-band/preview is dropped) and starts the new one; the **selection is preserved**. One dispatch site (`CommandProcessor::start_command`) | ribbon / dispatch | Implemented |
| **CIRCLE radius/[Diameter] option** — type `D` at the radius prompt (command line or DYN) to enter a diameter instead | C → D | Implemented (Ph26) |
| Parametric **multi-parameter dialog** (collect values → submit existing Command): ARRAY | ribbon | Implemented (Ph11) |
| POLYGON command + dialog | — | Planned (no POLYGON command yet) |
| **Import DWG** — runs an external converter (DWG→DXF) off-thread, then the existing DXF importer (fail-safe); writes a `<file>.dwg.import.log` gap catalog | ribbon / DWGIN | Implemented (Ph27; needs an installed converter — ODA File Converter or LibreDWG) |
| **Export DWG** — existing DXF export, then the external converter (DXF→DWG, default ACAD2018) | ribbon / DWGOUT | Implemented (Ph27; two-stage lossy, see ARCHITECTURE) |
| **DWG Setup** dialog — detect/Browse/auto-detect the converter, links to downloads; saves the path setting (offered via "Configure…" when none is found) | ribbon "DWG Setup" | Implemented (Ph27) |
| DWG converter path (configurable) | `io/dwg_converter_path` setting / DWG Setup dialog | Implemented (Ph27; auto-detects ODA/LibreDWG on PATH otherwise) |
| Per-entity linetype scale (CELTSCALE) | — | Planned |
| Lineweight property (hundredths-mm) | — | Implemented (model + round-trip; visible weight Planned) |
| LAYER command-line alias / PROPERTIES palette / MATCHPROP | LA / PR / MA | Planned (Phase 13) |

## Blocks / Reference

Block **definitions** + **INSERT** references (transform-at-snapshot, not exploded). This
phase covers **import, display, and selection**; in-app authoring is staged.

| Command | Alias | Status |
|---|---|---|
| Block import (BLOCKS + INSERT, nested, scale/rotation) from DWG/DXF | File ▸ Import | Implemented (Ph28) |
| Display block instances (definition × transform, resolved at snapshot; per-instance colour/layer; batched — N instances add no draw calls) | — | Implemented (Ph28) |
| Select / hover / window-crossing a block as one object; move / erase / copy the instance (not the definition); insertion-point grip; INS osnap | click / grips / Modify | Implemented (Ph28) |
| BLOCK / WBLOCK — define a block from selected geometry | B | Staged (authoring half) |
| REFEDIT — edit a definition; all instances update | — | Staged |
| EXPLODE — instance → its geometry | X | Staged |
| ATTDEF / ATTRIB — block attribute text | ATT | Implemented (issue #25: ATTDEF, INSERT value prompts, ATTDISP, ATTEDIT; DXF ATTDEF and INSERT+ATTRIB) |
| XREF | XR | Implemented (issue #25): attach / detach / reload / ?, re-read on open, native v32, DXF block flag 4 with the path |

## File / Plot

| Command | Alias | Status |
|---|---|---|
| Native format **v15** (adds dimension text decoration) | — | Implemented (v1–v14 load) |
| Native format v5 (adds polyline arc bulges) | — | Implemented (v1–v4 load) |
| DXF LWPOLYLINE bulges (code 42, read/write) | — | Implemented (LibreCAD-verified) |
| DXF TEXT + DIMENSION (all subtypes) + LEADER + DIMSTYLE table | — | Implemented (leader label imports as separate TEXT) |
| LWDISPLAY (lineweight display on/off) | LWT ribbon toggle | Implemented |
| Lineweight display: DPI-anchored `px = mm × DPI/25.4`, zoom-independent, Default = 1px hairline (AutoCAD-accurate) | — | Implemented |
| NEW — opens a new untitled drawing in a **new tab** (existing tabs untouched) | Ctrl+N / "+" tab button | Implemented |
| OPEN (native .musa) — **always opens into a new tab**; unsaved work in the current tab is never overwritten | Ctrl+O | Implemented |
| SAVE / SAVE AS — operate on the **active** document (its own path) | Ctrl+S / Ctrl+Shift+S | Implemented |
| **Multiple documents as tabs** — N drawings open at once; each tab shows its name + a `*` dirty marker. ONE engine swaps the active document on switch; ONE viewport renders the active document's snapshot | tab strip | Implemented (Phase A) |
| **Switch tab** — click a tab, or cycle with Ctrl+Tab / Ctrl+Shift+Tab. Each tab restores its own camera (zoom/pan), selection, and undo/redo history | Ctrl+Tab / Ctrl+Shift+Tab | Implemented |
| **Close tab** — the × on each tab, or Ctrl+W. A dirty tab prompts Save / Discard / Cancel; closing the last tab leaves one empty "DrawingN" (never zero tabs) | × / Ctrl+W | Implemented |
| **Undo/redo is per document** — Ctrl+Z on tab A rewinds tab A's last op even after edits in tab B | Ctrl+Z / Ctrl+Y | Implemented |
| **Quit guard** — closing the window prompts to save every dirty document (Cancel aborts the quit) | — | Implemented |
| **Cross-document copy/paste** — Ctrl+C / Ctrl+X copy/cut the selection to an in-process clipboard; Ctrl+V pastes into the ACTIVE document at the cursor, remapping layer/dimstyle/block references by NAME (creating any missing in the target). One undo group; the clipboard survives switching/closing the source | Ctrl+C / Ctrl+X / Ctrl+V | Implemented (Phase B) |
| **Tab-to-tab drag** — drag a selection onto another document's tab to transfer it there (copy → switch → paste, original coordinates) | drag to tab | Implemented (Phase B) |
| DXF export (R2000 / AC1015; LAYER table + ByLayer colour 256) | File ▸ Export DXF | Implemented |
| DXF import (LINE/LWPOLYLINE/CIRCLE/ARC/POINT/TEXT/MTEXT/DIMENSION/LEADER; BLOCK defs + INSERT refs; reads the LAYER table + ACI colours) | File ▸ Import DXF | Implemented |
| DXF import (SPLINE / legacy POLYLINE) | — | Implemented (issue #31): uniform-knot and fit-point splines come in as splines, other knot vectors as polylines; the R12-era POLYLINE/VERTEX/SEQEND form comes in as a polyline (bulges, closed flag, inside blocks too), while 3D polylines and meshes are reported as skipped |
| Dirty tracking (modified `*` in title, prompt before discard) | — | Implemented |
| PLOT / PRINT (PDF + installed printers; paper/orientation/area Display·Extents·Window/scale fit·ratio/centre·offset/lineweights/CTB None·Mono·Grayscale/copies; window-pick; print-preview; off-thread; vector output) | Ctrl+P / PLOT / PRINT | Implemented (Phase 30) |
| Saved page setups (named, persisted in the drawing; recall in the PLOT dialog) | PLOT ▸ Page setup | Implemented (Phase 30) |
| **Command line** — `musacad <drawing>` opens a file (via the existing OpenDocumentCommand); `musacad --check <drawing>` validates it and exits non-zero on a parse error; `--help` / `--version`. Parsed before any Qt object exists, so it needs no display | shell | Implemented (issue #11) |
| **Headless plot** — `musacad --plot <drawing> <out.pdf> [--paper A4] [--portrait\|--landscape] [--scale 1:5\|--fit] [--window x0,y0,x1,y1\|--extents] [--monochrome]`. Same loaders, snapshot builder, paper table, tessellation rule and PDF writer as the GUI's PLOT; offscreen Qt platform forced, so it runs from cron/CI/ssh. Exit codes 0/1/2/3 | shell | Implemented (issue #11) |

## Object snap (OSNAP)

| Mode | Status |
|---|---|
| Endpoint | Implemented |
| Midpoint | Implemented |
| Center | Implemented |
| Intersection | Implemented |
| Nearest | Implemented |
| Quadrant (circle/arc N/E/S/W) | Implemented |
| Node (Point entities) | Implemented |
| Perpendicular (deferred; line + circle/arc) | Implemented |
| Tangent (deferred; circle/arc) | Implemented |
| Centroid of closed polyline — **Musa extension** (no AutoCAD equivalent) | Implemented |
| Apparent intersection / Insertion / Parallel | Implemented (issue #32; Apparent intersection and Parallel are opt-in, as in AutoCAD) |

OSNAP precedence (highest→lowest, within the aperture): Endpoint, Midpoint,
Center, Node, Quadrant, Intersection, Perpendicular, Tangent, Centroid, Nearest.
Each type is individually toggleable via the running-osnap mask (OSNAP status-bar
button dropdown).

## Selection & live preview

| Feature | Status |
|---|---|
| Single-click pick (pick-box) | Implemented |
| Window select (left→right, enclosed) | Implemented |
| Crossing select (right→left, touched) | Implemented |
| Shift to add / Esc to clear / Select all | Implemented |
| Selected-entity highlight (orange) | Implemented |
| Rollover (hover) highlight (light blue) | Implemented |
| Live cursor preview (Line/Circle/Rect/PLine/Arc) | Implemented |
| Move/Mirror ghost preview | Implemented |
| Ortho/Polar/grid-snap honored by preview | Implemented |

## Grip editing (direct manipulation)

| Feature | Status |
|---|---|
| Grips (blue squares) show on a selected entity; grabbed/hovered grip goes hot (red) | Implemented |
| Drag a grip → live preview; release commits as one undo step; Esc cancels (entity unchanged) | Implemented |
| Zero store mutation / op-log churn during the drag (transient preview) | Implemented |
| ORTHO/POLAR/OSNAP honored on the dragged grip | Implemented |
| Line (2 endpoints + midpoint-move) | Implemented |
| Circle (centre-move + 4 quadrant-radius) | Implemented |
| Arc (centre-move + 2 endpoints + mid-radius) | Implemented |
| Polyline / Rectangle (per-vertex move) | Implemented |
| Hatch (per-boundary-vertex move → reshape the filled region) | Implemented |
| Text (insertion-point move) | Implemented |
| Dimension: full grip set — both ext-line origins, both dim-line ends, offset midpoint (grab anywhere, place anywhere) | Implemented (Linear/Aligned) |
| Dimension: **dim-line offset** (move the dim line, value unchanged) | Implemented |
| Dimension: def-point drag (re-measures, live value) | Implemented (Linear/Aligned/Radius/Diameter/Angular) |
| HiDPI: lineweights + grip/snap/crosshair sizes are the same physical size on 1×/2× displays (DPR-corrected) | Implemented |
| Dimension: independent text-reposition grip (all five types) + connector leader + PR "home text" | Implemented (issue #21) |
| Add/remove polyline vertex via grips | Implemented (issue #32): a midpoint grip per segment (drag moves a straight segment, reshapes an arc); right-click a grip for Add Vertex / Remove Vertex / Convert to Arc / Convert to Line |

## Ribbon (responsive + contextual)

The ribbon is registry-driven (icons + tooltips from each command's registration) and
**responsive**: as the window narrows, panels degrade FULL → COMPACT (secondary buttons
become icon-only) → COLLAPSED (the panel folds to one fly-out button that pops out the full
panel inline), lowest-priority panels first; a horizontal scroll bar is the final fallback.

**Contextual tabs** appear automatically when the whole selection matches an entity family,
and disappear (returning to the last fixed tab) when it doesn't. Mixed selections show none.

| Contextual tab | Appears when | Controls | Status |
|---|---|---|---|
| **Hatch Editor** (green accent) | all selected objects are hatches | Pattern picker, Solid-fill toggle, Scale, Angle, Re-hatch | Implemented |
| **Text Editor** (blue accent) | all selected objects are the Text family (TEXT/MTEXT/leaders) | Font, Height, Edit Text | Implemented |
| **Block Editor** (amber accent) | a single block reference (INSERT) is selected | Edit Block | Present (Edit Block staged — block authoring not yet implemented) |

## Status-bar modes & keys

| Mode | Key | Status |
|---|---|---|
| Object Snap | F3 | Implemented |
| Grid display | F7 | Implemented |
| Ortho | F8 | Implemented |
| Snap (grid snap) | F9 | Implemented |
| Polar tracking | F10 | Implemented |
| Undo / Redo | Ctrl+Z / Ctrl+Y | Implemented |
| Delete selection | Delete / Backspace | Implemented |
| Clear selection | Esc | Implemented |

## Coordinate entry

| Form | Example | Status |
|---|---|---|
| Absolute | `10,20` | Implemented |
| Relative | `@30,0` | Implemented |
| Polar relative | `@30<45` | Implemented |

## Command-line UX

| Feature | Status |
|---|---|
| Table-driven aliases | Implemented |
| Autocomplete dropdown (registry-driven) | Implemented |
| Up/Down history, ENTER repeats last | Implemented |
| Honest command results (engine echoes what actually happened) | Implemented |
| Parametric input dialog (ARRAY: rectangular + polar) | Implemented |
| Input dialogs for Rotate/Scale + live ghost preview | Planned (Phase 11.2) |
| Dynamic input tooltips at cursor | Implemented (full canvas DYN: command entry + autocomplete, mid-command sub-prompts, on-geometry dimension fields for RECTANGLE/LINE/CIRCLE; F12 toggles canvas-only ⇄ classic bottom bar) |
