# Musa CAD — Commands & Shortcuts (living roadmap)

This is the living specification of Musa CAD's AutoCAD-style commands. Each row
has a **Status**: `Implemented`, or `Planned (Phase N)`. Every future phase
implements a slice and flips rows to `Implemented`. Phase numbers beyond the
current one are a tentative roadmap and may shift.

Conventions: type the **Alias** or full name at the command line (autocomplete
suggests both); aliases are case-insensitive.

## Draw

| Command | Alias | Status |
|---|---|---|
| LINE | L | Implemented |
| PLINE (polyline) | PL | Implemented |
| CIRCLE | C | Implemented |
| ARC (3-point) | A | Implemented |
| RECTANGLE | REC | Implemented |
| SPLINE | SPL | Planned (Phase 7) |
| ELLIPSE | EL | Planned (Phase 7) |
| POLYGON | POL | Planned (Phase 7) |
| POINT | PO | Planned (Phase 7) |
| XLINE / RAY | XL | Planned (Phase 7) |
| HATCH / GRADIENT | H | Planned (Phase 10) |

## Modify

| Command | Alias | Status |
|---|---|---|
| ERASE (Last / All / pick) | E | Implemented |
| UNDO | U | Implemented |
| REDO | — (Ctrl+Y) | Implemented |
| MOVE | M | Implemented |
| COPY | CO / CP | Implemented |
| MIRROR | MI | Implemented |
| OFFSET (line/circle/arc/polyline) | O | Implemented |
| ROTATE | RO | Implemented |
| SCALE | SC | Implemented |
| ARRAY (rectangular + polar, command-line) | AR | Implemented |
| TRIM a line (cut by line/circle/arc edges) | TR | Implemented |
| TRIM an arc/circle/polyline *entity* | TR | Partial (line entities only; curve entities deferred) |
| EXTEND a line (to line/circle/arc boundary) | EX | Implemented |
| EXTEND an arc/polyline *entity* | EX | Partial (line entities only) |
| FILLET (line/line; radius 0 or tangent arc) | F | Implemented |
| FILLET (polyline corner → a true arc segment / bulge, dimensionable) | F | Implemented |
| Polyline arc segments (per-vertex bulge, AutoCAD LWPOLYLINE) | — | Implemented |
| FILLET (arc/curve cases) | F | Partial (line + polyline-corner only) |
| CHAMFER (line/line; Distance or Angle method, 45° default) | CHA | Implemented |
| CHAMFER (polyline corner) | CHA | Implemented |
| ARRAY dialog (interactive grid/preview) | AR | Planned (Phase 13) |
| STRETCH | S | Planned (Phase 13) |
| EXPLODE | X | Planned (Phase 13) |
| JOIN | J | Planned (Phase 13) |

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
| LEADER (simple arrow + line + single-line label, kept for compat) | LEADER | Implemented |
| **DIM (smart all-in-one; hover previews the type, dispatches by entity)** | DIM | Implemented (line/poly→linear, circle→diameter, arc→radius) |
| DIMLINEAR (two-point, or `[Object]` → select a line / polyline segment) | DLI | Implemented |
| DIMALIGNED (two-point, or `[Object]` → segment's true length) | DAL | Implemented |
| DIMRADIUS (**select a circle/arc**, or a filleted polyline arc segment → R) | DRA | Implemented |
| DIMDIAMETER (**select a circle/arc** → ⌀ from its own geometry) | DDI | Implemented |
| DIMANGULAR (**select two lines/edges** → angle from their directions) | DAN | Implemented |
| Arrowheads: filled / open / tick / dot (solid filled geometry) | DIMSTYLE | Implemented |
| DIMSTYLE: text height / arrow type+size / precision / ext lines | Dim Style btn | Implemented (Standard editable; multi-style manager Planned) |
| DIMSTYLE per-element colours (dim / ext / text / arrow) + dim lineweight | Dim Style btn | Implemented |
| Object-aware dims capture **def points** at creation (no entity ref) | — | Implemented (deleting the source entity never dangles the dim) |
| Placement preview: the full dimension (with live value) rubber-bands to the cursor, commits on click | — | Implemented (all dim types + DIM; angular arc is fixed by its two lines) |
| Associativity: value recomputed from def points each rebuild | — | Implemented (moving the *referenced* entity does not auto-update) |
| DIMCONTINUE / DIMBASELINE | DCO / DBA | Planned |
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
| Off / Frozen layers skip rendering | — | Implemented |
| Locked layers (drawn, not selectable/modifiable) | — | Implemented |
| Linetype property (Continuous/Dashed/Center/Hidden) | — | Implemented (model + round-trip; visual stipple Planned) |
| Lineweight property (hundredths-mm) | — | Implemented (model + round-trip; visible weight Planned) |
| LAYER command-line alias / PROPERTIES palette / MATCHPROP | LA / PR / MA | Planned (Phase 13) |

## Blocks / Reference

| Command | Alias | Status |
|---|---|---|
| BLOCK / WBLOCK | B | Planned (Phase 11) |
| INSERT | I | Planned (Phase 11) |
| XREF | XR | Planned (Phase 11) |

## File / Plot

| Command | Alias | Status |
|---|---|---|
| Native format v5 (adds polyline arc bulges) | — | Implemented (v1–v4 load) |
| DXF LWPOLYLINE bulges (code 42, read/write) | — | Implemented (LibreCAD-verified) |
| DXF TEXT + DIMENSION (all subtypes) + LEADER + DIMSTYLE table | — | Implemented (leader label imports as separate TEXT) |
| LWDISPLAY (lineweight display on/off) | LWT ribbon toggle | Implemented |
| Lineweight display: DPI-anchored `px = mm × DPI/25.4`, zoom-independent, Default = 1px hairline (AutoCAD-accurate) | — | Implemented |
| NEW (native .musa) | Ctrl+N | Implemented |
| OPEN (native .musa) | Ctrl+O | Implemented |
| SAVE | Ctrl+S | Implemented |
| SAVE AS | Ctrl+Shift+S | Implemented |
| DXF export (R2000 / AC1015; LAYER table + ByLayer colour 256) | File ▸ Export DXF | Implemented |
| DXF import (LINE/LWPOLYLINE/CIRCLE/ARC/POINT; reads the LAYER table) | File ▸ Import DXF | Implemented |
| DXF import (SPLINE / legacy POLYLINE) | — | Planned (skipped + reported for now) |
| Dirty tracking (modified `*` in title, prompt before discard) | — | Implemented |
| PLOT / PRINT | Ctrl+P | Planned (Phase 13) |

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
| Apparent intersection / Insertion / Parallel | Planned (Phase 9) |

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
| Text (insertion-point move) | Implemented |
| Dimension: full grip set — both ext-line origins, both dim-line ends, offset midpoint (grab anywhere, place anywhere) | Implemented (Linear/Aligned) |
| Dimension: **dim-line offset** (move the dim line, value unchanged) | Implemented |
| Dimension: def-point drag (re-measures, live value) | Implemented (Linear/Aligned/Radius/Diameter/Angular) |
| HiDPI: lineweights + grip/snap/crosshair sizes are the same physical size on 1×/2× displays (DPR-corrected) | Implemented |
| Dimension: independent text-reposition grip | Planned (needs a stored text offset) |
| Add/remove polyline vertex via grips | Planned |

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
| Dynamic input tooltips at cursor | Planned (Phase 13) |
