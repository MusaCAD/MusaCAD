# Musa CAD — TODO / deferred work

Durable backlog of things intentionally deferred. Each item notes *why* it was
parked and *what done looks like*, so it can be picked up cleanly later.

## Properties palette — staged deep groups (deferred 2026-06-09)

**Status:** PR (Phase 22) ships the framework, the multiplicity/`*VARIES*` model, the
universal General group (Layer/Color/Linetype/Lineweight), a read-only Geometry group,
and the full Text/MTEXT group — all via a generic descriptor registry
(`core/properties_registry`) so a new group is *new rows in the table*, not new UI.

**Not yet built (same pattern, mark each by adding descriptors):**
- **Dimension** deep group: per-element colors (dim/ext/text/arrow), arrow type/size,
  precision, and dimstyle overrides. Tangled with DIMSTYLE; the universal props already
  expose its color/layer/linetype/lineweight today.
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

**Status:** Slice 1 shipped — a reusable `ParameterDialog` (see
[ARCHITECTURE.md](ARCHITECTURE.md)) with the **ARRAY** dialog (rectangular +
polar) wired to the ribbon. The command pipeline is untouched: a dialog only
collects parameters and submits the existing `Command`.

**Not yet satisfied — the full AutoCAD-style vision still to build:**

1. **Dialogs / dynamic input for *draw* commands**, not just modify:
   - CIRCLE — radius / diameter (and 2P, 3P, TTR options).
   - RECTANGLE — length × width (and area, rotation).
   - POLYGON — number of sides, inscribed/circumscribed, radius.
   - ARC, ELLIPSE, POINT, etc. — their option sets.
2. **Dialogs for the remaining modify commands**: ROTATE (angle, copy, reference),
   SCALE (factor, reference), plus a "pick base point" affordance that briefly
   yields to the viewport for one pick, then returns to the dialog.
3. **Live preview while editing dialog fields** — `ParameterDialog` already emits
   `valuesChanged()`. Needs a render-side overlay mode that draws N transformed
   ghost copies of the selection (array) or the previewed primitive (draw), so the
   result updates as the user types. Zero geometry round-trip (same discipline as
   the cursor preview / move ghost).
4. **Dynamic Input (DYN)** — the in-canvas input boxes at the cursor (AutoCAD F12),
   showing length/angle fields you can tab between, as an alternative to both the
   command line and modal dialogs.
5. **A declarative command schema** so each command declares its fields/options
   *once* and gets the command-line prompts, the dialog, and dynamic input for
   free (avoid hand-writing each surface). `DialogSpec` is the seed of this.

**Done looks like:** every draw/modify command offers the AutoCAD-equivalent
dialog or dynamic-input field set, with live preview, all converging on the same
`Command` messages — no parallel pipeline.

Tracking rows live in [COMMANDS.md](COMMANDS.md) under "Command-line UX".
