# Musa CAD — AutoCAD configuration catalog (living roadmap)

The configurable AutoCAD options relevant to a 2D drafter, as the roadmap for
Musa CAD's settings. **Status**: `Implemented` (with the phase it landed, e.g.
`Ph15`) or `Planned`. Grouped by where AutoCAD exposes them.

## DIMSTYLE — Lines tab

| Option | Status |
|---|---|
| Dimension-line color | Implemented (Ph14) |
| Dimension-line lineweight | Implemented (Ph14) |
| Dimension-line extend beyond ticks | Planned |
| Baseline spacing | Planned |
| Suppress dim line 1 / 2 | Planned |
| Extension-line color | Implemented (Ph14) |
| Extension-line lineweight | Planned |
| Extend beyond dim lines (ext_extension) | Implemented (Ph13) |
| Offset from origin (ext_offset) | Implemented (Ph13) |
| Suppress ext line 1 / 2 | Planned |
| Fixed-length extension lines | Planned |

## DIMSTYLE — Symbols & Arrows tab

| Option | Status |
|---|---|
| Arrowhead type (filled / open / tick / dot) | Implemented (Ph14) |
| Arrowhead size | Implemented (Ph13) |
| Arrowhead color (per dimstyle) | Implemented (Ph14) |
| Separate first/second arrowheads | Planned |
| Center marks (none / mark / line) | Planned |
| Leader arrowhead | Implemented (Ph14, shares arrow rendering) |
| Arc length symbol / dimension break | Planned |

## DIMSTYLE — Text tab

| Option | Status |
|---|---|
| Text height | Implemented (Ph13) |
| Text color | Implemented (Ph14) |
| Text placement (above / centered) | Implemented (Ph13) |
| Text style (font) | Implemented (Ph29: stroke font + system TrueType/OpenType faces; SHX-name substitution on import) |
| Text alignment (horizontal / aligned with dim line) | Implemented (Ph13, aligned) |
| Text offset from dim line | Planned |
| Fraction format / text frame | Planned |

## DIMSTYLE — Fit tab

| Option | Status |
|---|---|
| Overall scale (DIMSCALE) | Planned |
| Fit options when text doesn't fit | Planned |
| Text placement when moved | Planned (grips, next phase) |

## DIMSTYLE — Primary Units tab

| Option | Status |
|---|---|
| Linear precision (decimal places) | Implemented (Ph13) |
| Unit format (decimal / architectural / engineering) | Planned (decimal only) |
| Decimal separator | Planned |
| Round off | Planned |
| Prefix / suffix | Implemented (Ph14, built-in R / ⌀ / ° for radius/dia/angular) |
| Measurement scale factor | Planned |
| Suppress leading / trailing zeros | Planned |
| Angular precision / format | Implemented (Ph14, degrees) |

## DIMSTYLE — Alternate Units / Tolerances tabs

| Option | Status |
|---|---|
| Alternate units (mm/inch dual) | Planned |
| Tolerances (symmetric / deviation / limits) | Planned |

## Drawing-wide settings

| Variable | Meaning | Status |
|---|---|---|
| LWDISPLAY | Show lineweights on/off | Implemented (Ph14) |
| Default lineweight | New-entity/ByLayer default (0.25 mm) | Implemented (Ph12) |
| Lineweight display mapping | Fixed-screen, DPI-anchored: `px = mm × DPI/25.4` (DPI from `QScreen::physicalDotsPerInch`; 96 DPI default ⇒ 3.7795 px/mm), zoom-independent, Default = 1px hairline | Implemented (Ph15, AutoCAD-accurate) |
| Lineweight ladder | Standard set 0.00…2.11 mm; mapping continuous over it | Implemented (Ph15) |
| Lineweight display *to scale* (world-proportional, thickens with zoom) | Alternative AutoCAD mode | Planned |
| LTSCALE | Global linetype scale | Implemented (Ph23; native + DXF $LTSCALE round-trip) |
| CELTSCALE | Per-entity linetype scale | Planned |
| Per-layer / per-entity lineweight | ByLayer + override | Implemented (Ph12) |
| Per-layer / per-entity color, linetype | ByLayer + override | Implemented (Ph12) |
| LUNITS / LUPREC | Drawing unit format / precision | Planned |
| PDMODE / PDSIZE | Point display style / size | Planned |
| Linetype patterns (dashed/center/hidden render) | Visual dashes | Implemented (Ph23; arc-length dashing on lines + curves, scaled by LTSCALE) |

## Properties palette (PR / Ctrl+1)

| Option | Status |
|---|---|
| Open/toggle palette (PR / PROPERTIES / PROPS / CH) | Implemented (Ph22) |
| Reflects selection: nothing / one / many-same / many-mixed | Implemented (Ph22) |
| **\*VARIES\*** for fields that differ across a multi-selection; edit sets all | Implemented (Ph22) |
| General: Layer / Color / Linetype / Lineweight (ByLayer or override) | Implemented (Ph22) |
| Geometry group (line length/ends, circle/arc center+radius, text position) | Implemented (Ph22, read-only) |
| Geometry numeric editing (type a new radius/length/endpoint) | Planned |
| Text/MTEXT group (contents, height, rotation, justify, width factor, line spacing, defined width, attachment) | Implemented (Ph22) |
| Font selection field | Implemented (Ph29: PR Font dropdown lists Standard + system TTF/OTF faces; sets the entity font, persists, re-renders) |
| Dimension group: per-dimension overrides (arrow type/size, dim/ext-line color, text height/color/placement, precision; ByStyle-or-Overridden + reset) | Implemented (Ph24; native round-trip, DXF override-vs-style native-only) |
| Quick-select / filter by property | Planned |

## Text / MTEXT

| Option | Status |
|---|---|
| TEXT (single-line) | Implemented (Ph13) |
| MTEXT (multi-line paragraph, word-wrap within a defined width) | Implemented (Ph20) |
| MTEXT attachment point (TL/TC/TR/ML/MC/MR/BL/BC/BR) | Implemented (Ph20) |
| MTEXT line spacing / width factor / rotation | Implemented (Ph20) |
| MTEXT inline formatting (bold, mid-string colour/height, stacked fractions) | Planned (Ph20 stub) |
| Text/font selection (multiple fonts, TTF) | Implemented (Ph29: TTF/OTF as filled glyphs via the fill pipeline; stroke font stays the default/fallback) |

## Leader / MLEADER

| Option | Status |
|---|---|
| LEADER (simple arrow + line + single-line text) | Implemented (Ph14) |
| QLEADER (arrow + multi-vertex line + attached MTEXT, editable) | Implemented (Ph20) |
| Leader–text association (text owned by the leader; moves with it) | Implemented (Ph20) |
| Leader grips (arrow tip, each vertex, text position) | Implemented (Ph20) |
| MLEADER content blocks / DXF MLEADER round-trip | Planned (DXF writes LEADER + MTEXT; native v6 is lossless) |
| Leader landing / dogleg auto-geometry | Planned |
| Leader style (own table) | Planned (uses dimstyle arrow for now) |

## RECTANGLE (RECTANG) options

Typed mid-command as option keywords (the same state-machine pattern as CIRCLE's
`[Diameter]`), matching AutoCAD's `RECTANG` prompts.

| Option | Prompt | Status |
|---|---|---|
| **Dimensions** (`D`) | length → width, then the cursor quadrant flips placement (NE/NW/SE/SW); click commits | Implemented |
| **Area** (`A`) | area → `[Length/Width]` → that side; the other = area / side; same quadrant-flip placement | Implemented |
| **Rotation** (`R`) | angle (degrees); the rectangle is rotated about the first corner | Implemented |
| First-corner **Chamfer** (`C`) | two chamfer distances applied to each corner | Planned |
| First-corner **Fillet** (`F`) | fillet radius applied to each corner | Planned |
| First-corner **Width** (`W`) | polyline line-width for the rectangle | Planned |
| First-corner **Elevation** (`E`) / **Thickness** (`T`) | Z elevation / 3D thickness | Planned (2D engine; Z staged) |

Notes: a non-numeric entry at a value prompt reverts to the plain other-corner pick (you
are never trapped); Esc cancels. With Dynamic Input on (the default), each of these steps
renders on the canvas at the cursor: the `[Area/Dimensions/Rotation]` keyword is typed at
the other-corner step, then **Length**/**Width**/**Area**/**Rotation** each appear as an
at-cursor **sub-prompt cell** (driven by `PreviewSpec::scalar_prompt`, distinct from the
on-geometry Length/Width drag fields), and the size, once fixed, previews as a
quadrant-flipped rectangle for the final corner pick. The earlier deferred context-aware
DYN-box item is now closed — see [TODO.md](TODO.md).

## OFFSET — entity handling

`OFFSET` prompts for a distance, an object, and a side point (AutoCAD-standard). The result
inherits the source entity's layer/properties.

| Entity | Behaviour | Status |
|---|---|---|
| Line | Parallel line at ±distance on the picked side | Implemented |
| Circle / Arc | Concentric (radius ± distance); shrinking past radius 0 fails cleanly | Implemented |
| **Polyline (open/closed, incl. rectangles & bulged/filleted corners)** | Each segment offset (lines parallel, arcs concentric with the bulge kept), corners **re-mitered** as the intersection of adjacent offset curves — line/line, line/arc, and arc/arc (via the shared line_line / line_circle / circle_circle primitives) — uniform spacing, clean corners (no trapezoid). An over-large inward offset that would fold the shape fails with "Offset distance too large for this polyline." and leaves the geometry unchanged | Implemented |
| `[Through/Erase/Layer]` options + multiple (repeat) | AutoCAD OFFSET sub-options | Planned (distance + side only today) |

## JOIN

| Step | Behaviour | Status |
|---|---|---|
| **Select objects, then JOIN** (noun-verb) | Every selected line / arc / open polyline that shares endpoints (within the snap tolerance) merges — each connected chain becomes ONE polyline (arcs → bulged segments), inheriting the source's layer/properties. The merged polyline is a single entity, so moving it or dragging a grip keeps it connected | Implemented |
| Multiple chains in one selection | Each connected group becomes its own polyline; an object that connects to nothing else selected stays separate | Implemented |
| Closed loop | A chain whose ends meet within tolerance becomes a **closed** polyline (which then OFFSETs uniformly) | Implemented |
| No pre-selection | Falls back to picking a source object, then targets | Implemented |
| Undo | The whole join is one undo group | Implemented |

Alias `J`; Modify-panel ribbon button. Closed polylines (no free endpoints) and non-curve
entities are not joinable and are left untouched.
