# Musa CAD — TODO / deferred work

Durable backlog of things intentionally deferred. Each item notes *why* it was
parked and *what done looks like*, so it can be picked up cleanly later.

## DWG/DXF import fidelity backlog (Phase 27)

DWG import (via the external converter) and DXF import both route through the one
DXF importer, which **catalogs** what it cannot yet represent rather than silently
dropping it (the per-type summary in the `IoResult` message + the `.import.log`
written next to each imported DWG). The importer handles LINE, POINT, ARC, CIRCLE,
LWPOLYLINE, TEXT, MTEXT, DIMENSION, LEADER.

**Fixed from real-DWG testing (Phase 27.1):**
- Lineweight code 370 negative sentinels (`-1/-2/-3` = ByLayer/ByBlock/Default) and
  out-of-range values were cast to a literal uint8 (→ 2.5mm fat lines everywhere); now
  only 0..211 are explicit widths, the rest inherit.
- MTEXT inline formatting runs (`\fCambria|…;`, `\C1;`, `{…}`, `\A1;`, …) rendered
  verbatim as garbage; now converted to plain text (`\P`→newline, escaped literals
  kept, styling runs dropped). Long group-3 chunks are concatenated. TEXT `%%c/%%d/%%p`
  overrides decoded.

That catalog is the prioritized migration roadmap — entity types still **skipped**,
roughly in value order for real-world drawings:

1. **INSERT / BLOCK** — block-definition table + reference instancing (position/scale/
   rotation, nested blocks, ATTRIB). **Highest value: most real DWGs are mostly blocks,**
   so today a block-heavy file imports nearly empty (e.g. the vendor flange: only its few
   top-level lines come in; ~200 lines live inside block defs). Needs a block table on the
   document + an INSERT expander (explode-on-import is the cheap first cut; true block
   instances later).
2. **Old-style POLYLINE / VERTEX** — heavyweight 2D and 3D polylines (vs LWPOLYLINE);
   some converters/older DWGs emit these. Bulges, widths, closed flag.
3. **ELLIPSE** — major/minor axis + ratio + param range; approximate as a polyline first.
4. **SPLINE variants** — NURBS/control-point/fit-point splines beyond current support;
   tessellate to a polyline.
5. **HATCH / solid fills** — boundary loops + pattern fill (needs a fill model). At least
   import the boundary as polylines so the shape is visible.
6. **SOLID / 3DFACE / WIPEOUT** — filled triangles/quads and masks.
7. **ATTRIB / ATTDEF** — block attribute text (titles, tags) — pairs with INSERT.
8. **IMAGE / OLE2FRAME** — raster/OLE underlays (needs a raster layer).
9. **Exotic / complex linetypes** — patterns with embedded text or shapes, beyond the
   Continuous/Dashed/Center/Hidden set.
10. **VIEWPORT, ACAD_PROXY_ENTITY, tables, fields, dynamic blocks, 3D solids** — paper-
    space/out-of-2D-scope or large; lowest priority.

**Why parked:** each is a substantial entity + render + persistence addition; the
external-converter import works today for the supported set and honestly reports the
rest. **Done looks like:** each type gets a DocEntity + store kind + DXF read/write +
render, removing it from the skip list; verify against a DWG/DXF that exercises it.
**Order of attack:** INSERT/BLOCK first (unlocks the most geometry), then the curve
types (POLYLINE/ELLIPSE/SPLINE), then fills (HATCH/SOLID). **DWG export** is two-stage
lossy (Musa→DXF→converter→DWG); raising DXF-export fidelity (above) lifts it too.

## Properties palette — staged deep groups (deferred 2026-06-09)

**Status:** PR (Phase 22) ships the framework, the multiplicity/`*VARIES*` model, the
universal General group (Layer/Color/Linetype/Lineweight), a read-only Geometry group,
and the full Text/MTEXT group — all via a generic descriptor registry
(`core/properties_registry`) so a new group is *new rows in the table*, not new UI.

**Not yet built (same pattern, mark each by adding descriptors):**
- **Dimension** deep group — DONE (Phase 24): per-dimension overrides for arrow type/
  size, dim/ext/text colour, text height/placement, precision (ByStyle-or-Overridden +
  reset), native v8 round-trip. Remaining dim follow-ups: override `arrow_color`,
  `ext_offset`/`ext_extension`, `dim_lineweight` (only the 8 essentials are wired);
  **DXF dim-override fidelity** (overrides are native-only today — map to DXF dimvar
  XDATA when worth it); the standalone LEADER's arrow override.
- **Numeric geometry editing**: make the Geometry group's line length/endpoints,
  circle/arc center+radius, etc. *editable* (a write fn + a non-ReadOnly editor on the
  existing descriptors) rather than display-only.
- **Font system**: the PR Font field is wired but read-only (single stroke font). When a
  real font table lands, make the field a live combo (no UI change needed).
- **Quick-select / property filter**: select-by-property using the same registry reads.

## MTEXT inline (per-character) formatting (deferred 2026-06-08)

**Status:** MTEXT (Phase 20) implements real multi-line layout, word-wrap, and all
**paragraph-level** fields (height, width factor, line spacing, attachment, rotation,
colour). Per-character inline formatting is intentionally **not** implemented (and not
faked).

**Why parked:** inline runs need an AutoCAD-style formatting-code parser (`\f`/`\C`/
`\H`/`\S`…) plus a richer stroke-font engine (real bold/italic, stacked fractions),
and the paragraph-level fields are what the Properties palette needs first.

**What done looks like:** parse inline codes in the stored content; lay out per-run
font/colour/height/stacking; round-trip the codes through native + DXF MTEXT; expose
run formatting in the Properties palette. The content string already round-trips
verbatim, so adding a parser later is non-breaking.

## Dialog boxes / dynamic input everywhere (deferred 2026-06-06)

**Status:** The cursor surface is settled. Draw/transform commands are **interactive**
(ribbon starts the command, pick on screen — the AutoCAD model); the cursor value box
is **DYN** (Phase 25), which mirrors the command line, with an **autocomplete dropdown**
(Phase 26) and **option keywords** surfaced in both (e.g. CIRCLE radius/[Diameter]).
The **ARRAY** multi-parameter dialog (Ph11 `ParameterDialog`) remains for genuinely
parametric commands. Earlier upfront draw modals were removed — a modal that asks for
position isn't "identical to the command line"; DYN is. The command pipeline is
untouched (collect-and-submit only).

**Remaining:**

1. **More command options as keywords** (work in command line + DYN): CIRCLE 2P/3P/TTR;
   RECTANGLE area/rotation; ARC/ELLIPSE option sets. Each is a command state-machine
   keyword, surfaced via the mirrored prompt.
2. **POLYGON** — no POLYGON command exists yet (build the command + its sides/
   inscribed-circumscribed/radius options).
3. **Context-aware Dynamic Input box (deferred 2026-06-09)** — TODAY the DYN box is a
   faithful *mirror* of the command line (prompt + a generic value field); it is
   correct but adds little beyond the command line. AutoCAD's DYN box is
   **context-sensitive**: its fields and structure change with the active command,
   the current step, and the selected object — e.g. CIRCLE shows a radius field with a
   radius/diameter switch *in the box*; RECTANGLE shows length + width fields with the
   visual rubber-band; each command surfaces its own helpers/option chips right there.
   It is not a fixed always-on layout.
   - **Why parked:** purely UI/UX (no functional gap — every value is already
     enterable via the command line / the generic DYN field). It needs a per-command
     *field schema* the DYN box renders (which dovetails with item 4), plus
     option-keyword chips / a Down-arrow dropdown for the bracketed `[Diameter]`-style
     options (today they are typed).
   - **Done looks like:** selecting/typing a command drives the DYN box to show that
     command's labelled fields + option chips for the current step; editing them
     submits the same `Command` (no parallel pipeline) — the box becomes a genuine
     shortcut, not just an echo.
4. **A declarative command schema** so each command declares its fields/options
   *once* and gets the command-line prompts, the dialog, and dynamic input for
   free (avoid hand-writing each surface). `DialogSpec` is the seed of this.

**Done looks like:** every draw/modify command offers the AutoCAD-equivalent
dialog or dynamic-input field set, with live preview, all converging on the same
`Command` messages — no parallel pipeline.

Tracking rows live in [COMMANDS.md](COMMANDS.md) under "Command-line UX".
