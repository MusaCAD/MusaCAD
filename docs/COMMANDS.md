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
| MOVE | M | Planned (Phase 7) |
| COPY | CO | Planned (Phase 7) |
| ROTATE | RO | Planned (Phase 7) |
| SCALE | SC | Planned (Phase 7) |
| MIRROR | MI | Planned (Phase 7) |
| OFFSET | O | Planned (Phase 7) |
| TRIM | TR | Planned (Phase 7) |
| EXTEND | EX | Planned (Phase 7) |
| FILLET | F | Planned (Phase 7) |
| CHAMFER | CHA | Planned (Phase 7) |
| ARRAY | AR | Planned (Phase 7) |
| STRETCH | S | Planned (Phase 7) |
| EXPLODE | X | Planned (Phase 7) |
| JOIN | J | Planned (Phase 7) |

## Annotate / Dimensions

| Command | Alias | Status |
|---|---|---|
| DIMLINEAR | DLI | Planned (Phase 8) |
| DIMALIGNED | DAL | Planned (Phase 8) |
| DIMANGULAR | DAN | Planned (Phase 8) |
| DIMRADIUS | DRA | Planned (Phase 8) |
| DIMDIAMETER | DDI | Planned (Phase 8) |
| DIMCONTINUE | DCO | Planned (Phase 8) |
| DIMSTYLE (arrows/units/precision) | D | Planned (Phase 8) |
| MLEADER / LEADER | MLD | Planned (Phase 8) |
| TEXT | DT | Planned (Phase 10) |
| MTEXT | MT | Planned (Phase 10) |
| STYLE (text style) | ST | Planned (Phase 10) |

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
| LAYER (layer manager) | LA | Planned (Phase 9) |
| PROPERTIES | PR / Ctrl+1 | Planned (Phase 9) |
| MATCHPROP | MA | Planned (Phase 9) |
| Color / Linetype / Lineweight | — | Planned (Phase 9) |

## Blocks / Reference

| Command | Alias | Status |
|---|---|---|
| BLOCK / WBLOCK | B | Planned (Phase 11) |
| INSERT | I | Planned (Phase 11) |
| XREF | XR | Planned (Phase 11) |

## File / Plot

| Command | Alias | Status |
|---|---|---|
| NEW | Ctrl+N | Planned (Phase 12) |
| OPEN | Ctrl+O | Planned (Phase 12) |
| SAVE / SAVEAS | Ctrl+S | Planned (Phase 12) |
| PLOT / PRINT | Ctrl+P | Planned (Phase 12) |
| DXF import / export | — | Planned (Phase 12) |

## Object snap (OSNAP)

| Mode | Status |
|---|---|
| Endpoint | Implemented |
| Midpoint | Implemented |
| Center | Implemented |
| Intersection | Implemented |
| Nearest | Implemented |
| Perpendicular / Tangent / Quadrant / Node | Planned (Phase 9) |

## Status-bar modes & keys

| Mode | Key | Status |
|---|---|---|
| Object Snap | F3 | Implemented |
| Grid display | F7 | Implemented |
| Ortho | F8 | Implemented |
| Snap (grid snap) | F9 | Implemented |
| Polar tracking | F10 | Implemented |
| Undo / Redo | Ctrl+Z / Ctrl+Y | Implemented |

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
| Dynamic input tooltips at cursor | Planned (Phase 9) |
