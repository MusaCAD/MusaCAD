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
| FILLET (polyline corner; arc approximated by vertices) | F | Implemented |
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
| **DIM (smart all-in-one; hover previews the type, dispatches by entity)** | DIM | Implemented (line/poly→linear, circle→diameter, arc→radius) |
| DIMLINEAR (two-point, or `[Object]` → select a line / polyline segment) | DLI | Implemented |
| DIMALIGNED (two-point, or `[Object]` → segment's true length) | DAL | Implemented |
| DIMRADIUS (**select a circle/arc** → R from its own geometry) | DRA | Implemented |
| DIMDIAMETER (**select a circle/arc** → ⌀ from its own geometry) | DDI | Implemented |
| DIMANGULAR (**select two lines/edges** → angle from their directions) | DAN | Implemented |
| LEADER (arrow + line + text label) | LE / LEADER | Implemented |
| Arrowheads: filled / open / tick / dot (solid filled geometry) | DIMSTYLE | Implemented |
| DIMSTYLE: text height / arrow type+size / precision / ext lines | Dim Style btn | Implemented (Standard editable; multi-style manager Planned) |
| DIMSTYLE per-element colours (dim / ext / text / arrow) + dim lineweight | Dim Style btn | Implemented |
| Object-aware dims capture **def points** at creation (no entity ref) | — | Implemented (deleting the source entity never dangles the dim) |
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
| Native format v4 (adds leaders + expanded DIMSTYLE) | — | Implemented |
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
| Grips (square handles on selection) | Planned (Phase 9) |

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
