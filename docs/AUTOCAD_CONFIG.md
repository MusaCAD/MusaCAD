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
| Text style (font) | Planned (single stroke font for now) |
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
| Font selection field | Implemented as read-only (single stroke font); real font system Planned |
| Dimension group (per-element colors, arrow type/size, dimstyle overrides) | Planned |
| Quick-select / filter by property | Planned |

## Text / MTEXT

| Option | Status |
|---|---|
| TEXT (single-line) | Implemented (Ph13) |
| MTEXT (multi-line paragraph, word-wrap within a defined width) | Implemented (Ph20) |
| MTEXT attachment point (TL/TC/TR/ML/MC/MR/BL/BC/BR) | Implemented (Ph20) |
| MTEXT line spacing / width factor / rotation | Implemented (Ph20) |
| MTEXT inline formatting (bold, mid-string colour/height, stacked fractions) | Planned (Ph20 stub) |
| Text/font selection (multiple fonts, TTF) | Planned (single stroke font; font ref stored) |

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
