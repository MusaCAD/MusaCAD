# Musa CAD — TODO / deferred work

Durable backlog of things intentionally deferred. Each item notes *why* it was
parked and *what done looks like*, so it can be picked up cleanly later.

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
