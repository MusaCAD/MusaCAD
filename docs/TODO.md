# Musa CAD — TODO / deferred work

Durable backlog of things intentionally deferred. Each item notes *why* it was
parked and *what done looks like*, so it can be picked up cleanly later.

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
