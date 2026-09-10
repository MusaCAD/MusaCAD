# Musa CAD — TODO / deferred work

Durable backlog of things intentionally deferred. Each item notes *why* it was
parked and *what done looks like*, so it can be picked up cleanly later.

## Raster images (issue #10 — model/seam/persistence/plot DONE; viewport + DXF staged)

* **Viewport rendering — staged.** An image currently **plots** but does not **display in
  the viewport**: the GPU abstraction has no texture type. **Done looks like:** `GpuTexture`
  in the public `render/gpu/` headers with a GL 4.6 DSA implementation
  (`glCreateTextures`/`glTextureStorage2D`/`glTextureSubImage2D`), an image shader pair, and
  a renderer-side texture cache keyed by definition index, invalidated when
  `ImageInstance::def_version` changes. Images are few, so a draw call per image is fine —
  but the **documented draw-call bound must be raised and re-proven** in
  `render_offscreen`, along with the "pan/zoom uploads 0 scene bytes" constraint. The
  snapshot side is already done and deliberately carries no pixels.
* **DXF `IMAGE` / `IMAGEDEF` — staged.** Nothing is written today. It needs the OBJECTS
  section plus an IMAGEDEF dictionary, which is more DXF structure than any existing
  entity uses. **Done looks like:** `IMAGE` entities referencing `IMAGEDEF` objects, with
  the fidelity loss counted in the import result string like every other gap.
* **IMAGEATTACH / IMAGECLIP commands + ribbon — staged.** The entity, its persistence and
  its plot path exist; there is no interactive command to place or clip one yet (fixtures
  and the format are the current route in). **Done looks like:** `IMAGEATTACH` picking a
  file and two corners, `IMAGECLIP` picking a rectangle, both submitting the existing
  `AddImageCommand`, with ribbon icons in the established style.
* **Embedded-payload size cap — staged.** Base64 embedding has no size limit or warning
  today. **Done looks like:** a cap (a few MB) above which the writer warns and suggests
  the external-path form.

## Dimensions — chaining (issue #28 partial)

* **DIMDLI (baseline spacing) as a style variable — staged.** DIMBASELINE currently derives
  its offset as 1.5 x the effective text height, which reproduces AutoCAD's ISO default
  (3.75 mm at 2.5 mm text) without a format bump. **Done looks like:** a `baseline_spacing`
  field on `DimStyle` with the usual override bit, and a version bump.
* **DIMORDINATE / DIMJOGGED / DIMARC — staged.** The cheap half of #28 (the two chaining
  helpers) is done; these three each need a new `DimType` and a branch in
  `compute_dim_geometry`, plus native/DXF round-trip.

## Tables (issue #22 DONE; refinements staged)

* **Cell editing — staged.** A table is placed with empty cells; there is no in-app way to
  type into one yet (fixtures and the format are the current route in). **Done looks like:**
  the Ph21 double-click gesture over a cell, reusing `EditTextContentCommand`'s
  capture→change→recommit shape. `table_cell_at()` already returns the cell index the
  gesture needs.
* **Row/column insert + delete — staged.** The grid shape is stored, so inserting a row is
  a capture→rebuild→recommit like any other edit; there is just no command for it.
* **DXF `ACAD_TABLE` — staged.** Nothing is written. It is a proxy-heavy entity; exporting
  the table as its constituent lines + text is probably more useful to most consumers, and
  is the recommended first step.
* **Row-height grips — deliberately absent.** Row heights are stored but follow the style's
  text heights in practice, so only column-boundary grips are published. Revisit if
  authors ask for per-row heights.

## GD&T (issue #8 DONE; interop + input surface staged)

* **DXF `TOLERANCE` export/import — staged.** GD&T is currently **native-only**: nothing is
  written to DXF and nothing is read back. AutoCAD's `TOLERANCE` entity carries the frame as
  a string with `%%v` field separators plus a dimstyle reference, which is a real interop
  path. Writing a half-valid entity would be worse than writing none, so the gap is stated
  rather than faked. **Done looks like:** `TOLERANCE` written with the composed cell string
  + dimstyle ref, read back into cells by splitting on `%%v`; datum symbols exported as
  leader + text + triangle geometry with the fidelity loss counted in the import result
  string, the way every other gap is.
* **ParameterDialog input surface for a feature control frame — staged.** A frame is
  genuinely multi-parameter, which is the case the "dialogs when a dialog genuinely fits"
  rule contemplates. The command-line Q&A (`TOLERANCE`/`TOL`) is implemented — the
  scriptable half — but the ribbon button starts the same Q&A rather than opening a dialog.
  **Done looks like:** a `DialogSpec` with a characteristic combo (the symbol set), tolerance
  value + ⌀/material-modifier toggles, and three datum + modifier rows, submitting the same
  `AddFcfCommand`.
* **Editing an existing frame's cells — staged.** The PR exposes a frame's *styling* (text
  height, colours) but not its variable-length cell list, so changing a tolerance value means
  re-running `TOLERANCE`. **Done looks like:** a content editor for the cell list, most likely
  the double-click gesture Ph21 gave text, reusing `EditTextContentCommand`'s
  capture→change→recommit shape.

## Dimensions

* **Short-leader variant for outside dimension text — deferred.** ISO 129-1 permits a
  narrow dimension's value to sit on a short leader instead of on the dimension line's
  extension. Issue #12 implements the extension placement (which is the commoner form and
  the one AutoCAD defaults to); the leader variant needs a stored leader direction/length
  per dimension plus a format field, and did not fall out of the fit work cheaply.
  **Done looks like:** a `TextFit::Leader` value drawing a landing line from the dimension
  line to the text, with the leader side chosen from the placement point. See
  `docs/ARCHITECTURE.md` ▸ "ISO 129-1 narrow-dimension fit".
* **Narrow-dimension fit for radius/diameter/angular — deferred.** The ISO 129-1 fallback
  in issue #12 covers **linear and aligned** dimensions, where "does the value fit between
  the extension lines" is well defined. Radius/diameter place their text along the leader
  from the centre and angular places it on the arc, so neither has the same collision; a
  cramped radius on a tiny fillet is a different (rarer) problem. **Done looks like:** a
  fit test for the radial forms, most likely reusing `dim_label_quad` against the arc.

## Release / packaging (v0.1.0 infra DONE 2026-06-23; items staged)

* **DONE.** Linux **AppImage** (`packaging/linux/`, bundles Qt6 + qsvg, verified locally),
  Linux **Flatpak** (`packaging/flatpak/`, KDE 6.10 runtime, builds + installs + runs locally),
  **Windows installer** via GitHub Actions (`.github/workflows/build-windows.yml` + NSIS), Linux
  AppImage CI (`build-linux.yml`), and `scripts/release.sh`. Playbook in `docs/RELEASING.md`.
* **Flathub submission — STAGED.** The manifest + AppStream metainfo are ready
  (`packaging/flatpak/com.musacad.MusaCAD.{yml,metainfo.xml}`); submission to Flathub is deferred
  until **after** the GitHub release. **Done looks like:** swap the manifest source to the tagged
  `type: git` release and open a PR on `github.com/flathub/flathub`. See
  `packaging/flatpak/BUILD_FLATPAK.md` ▸ "Flathub — STAGED".
* **macOS support — planned.** No `.app`/`.dmg` packaging yet (dev machine is Linux; no runtime
  verification possible here). **Done looks like:** a `macos-latest` workflow that builds, runs
  `macdeployqt`, and produces a signed/notarized `.dmg`.
* **Automated builds on tag push — done.** The Linux + Windows workflows trigger on `v*` tags
  and their `publish` jobs create the GitHub release and attach the AppImage and installer;
  the release notes and the Flatpak are still set by hand (`docs/RELEASING.md`).
* **MSVC `/W4` warning audit — staged.** The first Windows build surfaced latent MSVC `/W4`
  warnings (e.g. C4244 narrowing); the codebase predates any MSVC build. `/WX` on MSVC is gated
  behind `MUSACAD_MSVC_WERROR` (default **OFF**) so the Windows release isn't blocked, while
  GCC/Clang `-Werror` stays on. **Done looks like:** clean the MSVC `/W4` warnings and flip
  `MUSACAD_MSVC_WERROR` ON in CI. See `cmake/CompilerWarnings.cmake`.
* **Wayland platform plugin in the AppImage — deliberately absent.** The AppImage bundles
  `xcb`, `offscreen` and `minimal`. On a Wayland session it prints "Could not find the Qt
  platform plugin \"wayland\"" and falls back to XWayland, which works. Bundling Qt's wayland
  plugin needs matching `libwayland-*` client libraries inside the AppImage and is a common
  source of breakage, so the fallback is preferred. **Done looks like:** bundling it *and*
  verifying on both a Wayland and an X11 session before removing this note.
* **Windows installer hardware verification — staged.** v0.1.0's `.exe` is CI-produced and was
  published without a real-Windows-hardware launch test. **Done looks like:** install on a real
  Windows box, confirm it launches + draws + plots, and fix anything found.

## Ribbon (Phases A + B DONE 2026-06-23; staged items below)

* **DONE.** Registry-driven SVG icons + tooltips (A); dropdown grouping + scroll overflow
  (A-intermediate); three-state progressive panel collapse (FULL/COMPACT/COLLAPSED with a
  fly-out popout) + contextual tabs (Hatch/Text/Block editors, family-predicate driven,
  accent stripe) (B). See `docs/ARCHITECTURE.md` ▸ "Ribbon Phase B".
* **Block Editor authoring — staged.** The Block Editor contextual tab appears for a single
  INSERT but its **Edit Block** button is greyed out: in-place block-definition editing
  (BEDIT) is not implemented (blocks today come only from DXF/DWG import). **Done looks
  like:** a block-edit mode that opens the definition's entities for editing and writes them
  back to all instances. There is also no interactive block-create command (BLOCK/MAKEBLOCK)
  yet — a prerequisite.
* **Contextual control depth — staged.** The Hatch/Text editors expose the common controls
  (pattern/scale/angle; font/height/edit); richer parity with AutoCAD (e.g. MTEXT inline
  bold/italic, hatch boundary association editing, per-tab gradient controls) is deferred and
  tracked with the relevant feature backlogs (MTEXT formatting, associative hatch).

## HATCH (Parts A + B DONE 2026-06-23; refinements staged)

* **Part A — DONE.** `EntityKind::Hatch` + SOLID fill (exact trapezoidal triangulation
  with islands), two boundary modes (**pick internal point** with a planar-arrangement
  tracer that respects partitioning lines + islands, and **pre-selected** closed
  polylines), pickable/PR-editable/MATCHPROP/grips-reshape, native + DXF round-trip,
  derived-not-baked (plots as vectors). See `docs/ARCHITECTURE.md` ▸ "HATCH".
* **Part B — line patterns — DONE.** `.PAT` parser + a built-in stock library (ANSI31–38
  plus NET/GRID/BRICK/BOX/HEX/HONEY/ANGLE/DOTS/CROSS/SQUARE/TRIANG/GRASS/EARTH/STEEL/
  CONC/INSUL/…), authored from the public .PAT format (license-clean; load acad.pat via
  `parse_pat` for the vendor set). Line families generated + clipped to the boundary at
  render time (derived-not-baked, even-odd islands), scaled/rotated by HPSCALE/HPANG/
  HPORIGIN, routed into line batches so they plot as vectors. Command `[Pattern/Scale/
  Angle]` options. SOLID stays the special name (one render path). See
  `docs/ARCHITECTURE.md` ▸ "Line patterns".
* **Staged refinements.** Gap tolerance (HPGAPTOL parity) beyond the basic endpoint
  bridging now in place; loose-segment islands; associative hatches (boundary edits
  re-fill); per-hatch DXF pattern-line export (PAT data — currently the pattern *name*
  round-trips and re-resolves from the library, which covers the stock set); pattern
  preview/dialog in the UI; GRADIENT fills.

## Multi-document — Phase B (DONE 2026-06-18; refinements staged)

**Status:** Phases A and B are done. Phase A: N drawings as tabs, switch/close/new/open,
dirty prompts, per-document camera/selection/undo. Phase B: cross-document copy/paste/cut
(Ctrl+C/X/V) + tab-to-tab drag, with layer/dimstyle/block remapping by name and a recursive
block-definition closure for INSERTs, full layer/dimstyle/block/font remap, and a cycle
guard on malformed self-referential blocks (see [ARCHITECTURE.md](ARCHITECTURE.md)
"Cross-document clipboard"). **Staged refinement** (small, non-blocking):

1. **Same-named-but-different table merge policy** — paste reuses a target layer/dimstyle/
   block that shares a name even if its attributes differ (AutoCAD-style "keep destination").
   A future option could rename-on-conflict; today it silently reuses.

## Text quality — stroke text on screen (DONE 2026-06-18; refinements staged)

**Status:** Done. Single-stroke text stays the engineering default; three changes make it
read professional on screen without changing plotted ink (see [ARCHITECTURE.md](ARCHITECTURE.md)
"Stroke-text quality"): (1) a screen-only ~0.5 mm text weight via `ColorBatch::is_text`
through the existing thick-line pipeline (plot honours the entity's 0 weight → hairline, so
paper is ink-for-ink identical, verified 0-pixel diff); (2) real lowercase a–z in the
built-in stroke font (simplex/Hershey-class, true ascenders/x-height/descenders), replacing
the Phase-13 small-caps fallback, monospace metrics unchanged; (3) analytic ~1 px edge
antialiasing in `thickline.frag` + alpha blending. SHX→stroke substitution still routes to
the improved font. Both presets clean, ctest green.

1. **Per-text-height adaptive screen weight (DONE 2026-06-18)** — text batches now carry their
   quantised cap height (`ColorBatch::text_height`, keyed in `line_key`); the renderer caps the
   stroke at a fraction (~12%) of the glyph's on-screen height (`text_height × camera.scale()`),
   floored at a 1 px hairline. Tiny title-block fields read crisp; title-size text keeps full
   presence. Screen-only (plot ink 0-pixel diff, verified). **Staged refinement** (non-blocking):
2. **Fuller Hershey glyph coverage** — the hand-authored set covers ASCII + 3 CAD symbols.
   A future pass could fold in more of the public-domain Hershey occidental set (accents,
   extended punctuation) behind the same parser.

## Linetype scale — LTSCALE + CELTSCALE (DONE 2026-06-19; PSLTSCALE staged)

**Status:** Done. LTSCALE is the drawing scale (per-document, on `GeometryStore`); CELTSCALE
is the per-entity multiplier (sparse cold map; PR "Linetype scale" + MATCHPROP; native v12 +
DXF code 48). Effective dash scale = LTSCALE × CELTSCALE — one multiplication in
`scene_snapshot.cpp` feeding the Ph23 dash renderer + the Ph30 plot path (no fork). CELTSCALE
is therefore **off the staged list** (was noted as Planned in the Ph23/MATCHPROP reports).
**Staged:**

1. **PSLTSCALE** (paper-space linetype scaling) — AutoCAD scales dashes by the viewport zoom
   in paper space so they read consistently across scaled viewports. **Why parked:** needs
   paper-space **layouts** (deferred with the Ph30 layout work). **Done looks like:** a
   PSLTSCALE system var that, in a layout viewport, folds the viewport scale into the
   effective dash scale.
2. **Block-internal / dimension dashing** — block-internal (INSERT) and dimension geometry
   render solid today (they don't route through `dash_polyline`), so CELTSCALE has no effect
   there. **Done looks like:** route block-member + dim ext/dim lines through the dash walker,
   with the INSERT's CELTSCALE multiplying its contained entities' scale.

## Plotting / printing (Phase 30 follow-ups)

Phase 30 added the PLOT pipeline: a shared vector renderer (`paint_plot`) used by both
the PDF (`QPdfWriter`) and printer (`QPrinter`) targets, a dark PLOT dialog (paper /
orientation / area / scale / centre+offset / lineweights / CTB / copies), Ctrl+P + the
`PLOT` command + a ribbon button, window-pick from the viewport, a print-preview, a
fine-tolerance plot snapshot (crisp arcs at any paper scale), three built-in CTB plot
styles (None / Monochrome / Grayscale, with the universal near-white→black rule), and
saved page setups persisted in the native format (v11) + recalled in the dialog. The
converter/target call runs off the UI thread behind a modal indeterminate dialog; a
failed target degrades gracefully. Deferred:

- **Named CTB/STB plot-style tables** — today there are three built-in styles applied at
  plot time. **Why parked:** the built-ins cover the common "print it black" / grayscale
  needs without a per-colour pen-mapping editor. **Done looks like:** import/edit `.ctb`
  pen tables (per-ACI colour → plotted colour / lineweight / screening) selectable per plot.
- **Layouts / paper space** — Phase 30 plots model space (Display / Extents / Window).
  **Why parked:** model-space plotting is the 80% path; layout tabs are a larger feature
  (viewports, per-layout page setup, title-block blocks). **Done looks like:** named
  layout tabs each with their own page setup + one or more scaled model-space viewports.
- **Plot stamp, batch/publish, raster/shaded output** — no plot-stamp footer, no
  multi-sheet publish, vector-only (no rendered/raster mode). Parked as out-of-scope niceties.

## Fonts: true SHX parsing + style table (Phase 29 follow-ups)

Phase 29 added TrueType/OpenType rendering (filled glyphs via the fill pipeline) + an
SHX-name → TTF **substitution** table for imports. Deferred:

- **True SHX binary parsing** — render the actual `.shx` shape definitions instead of a
  TTF lookalike. **Why parked:** substitution is the standard, low-risk approach and covers
  imports faithfully (single-stroke CAD fonts have clean TTF equivalents). **Done looks
  like:** an SHX shape-file reader producing stroke geometry, selected when the real `.shx`
  is available, else substitute.
- **Text-style table as a first-class entity** (STYLE command; named styles bundling font +
  width factor + oblique). Font is **per-entity** today (a font-table index). **Done:** a
  style table the entity references, with the font as a style default.
- **Per-glyph kerning / inline rich runs** beyond the face's default advances and
  width_factor (width_factor is not applied to TTF glyph geometry today — a noted minor gap).

**The import substitution** (`QtFontEngine::substitute`): every `*.shx` (single-stroke CAD
shape font) → the built-in **stroke font** (its faithful single-stroke match); a TTF/OTF
family name or `*.ttf` → that installed face, with aliases for common proprietary families
(`arial`/`helvetica` → Liberation/DejaVu Sans, `times new roman` → Liberation/DejaVu Serif,
`courier new` → a mono face); anything unmatched → the stroke font.

## DWG/DXF import fidelity backlog (Phase 27)

DWG import (via the external converter) and DXF import both route through the one
DXF importer, which **catalogs** what it cannot yet represent rather than silently
dropping it (the per-type summary in the `IoResult` message + the `.import.log`
written next to each imported DWG). The importer handles LINE, POINT, ARC, CIRCLE,
LWPOLYLINE, TEXT, MTEXT, DIMENSION, LEADER.

**Fixed from real-DWG testing (Phase 27.1 / 27.2):**
- Lineweight code 370 negative sentinels (`-1/-2/-3` = ByLayer/ByBlock/Default) and
  out-of-range values were cast to a literal uint8 (→ 2.5mm fat lines everywhere); now
  only 0..211 are explicit widths, the rest inherit.
- MTEXT inline formatting runs (`\fCambria|…;`, `\C1;`, `{…}`, `\A1;`, …) rendered
  verbatim as garbage; now converted to plain text (`\P`→newline, `^I` caret-tab→space,
  escaped literals kept, styling runs dropped, but `\U+XXXX` Unicode escapes PRESERVED).
  Long group-3 chunks are concatenated. TEXT `%%c/%%d/%%p`/`%%o`/`%%u`/`%%nnn` overrides are
  now kept RAW on import and expanded at render time (`core::text::substitute_text_codes`,
  derived-not-baked), so the codes round-trip losslessly back out to DXF.
- **Colours** were all white: ACI colour (code 62) was ignored for layers and only set
  the by-layer flag (never the RGB) for entities. Now resolved through the standard ACI
  palette for both; true colour (420) still wins.
- MTEXT lines **overlapped** (baselines stacked exactly one cap-height apart). Now use
  AutoCAD single spacing (5/3 of cap height) and read the line-spacing factor (code 44).
- Dimension text placement (Above/Centered) was inverted for rotated/vertical dims;
  now offsets along the text's own up-vector so both axes agree.

**Still imperfect (lower priority, catalogued):**
- MTEXT paragraph alignment (`\pxqc;`/`\pxql;` = centre/left) is dropped — centred
  title text (e.g. "GENERAL NOTES") imports left-aligned.
- Per-run inline styling (font/bold/colour/height/stacked fractions) is intentionally
  flattened to plain text (Musa has no rich-text model).

**INSERT / BLOCK — DONE (Phase 28).** Real block entities: a block-definition table +
INSERT references with a transform, resolved (definition × transform) at snapshot — NOT
exploded. Import (BLOCKS + INSERT), display, select/hover/window, move/erase/copy, native
v9 + DXF round-trip. The vendor flange went from 14 visible lines to 12 block instances
over 53 definitions. **Staged (the editing half, deferred):** in-app BLOCK creation
(define from selection), REFEDIT redefinition (edit a definition, all instances update),
EXPLODE (instance → its geometry), ATTDEF/ATTRIB block attributes. **Partial in blocks:**
DIMENSION/LEADER/POINT/SPLINE entities *inside* a block definition are skipped (catalogued)
— block content supports line/circle/arc/polyline/text/mtext/nested-insert. ByBlock colour
is approximated (a ByLayer member on layer 0 inherits the insert's resolved props).

That catalog is the prioritized migration roadmap — entity types still **skipped**,
roughly in value order for real-world drawings:

1. **Old-style POLYLINE / VERTEX** — heavyweight 2D and 3D polylines (vs LWPOLYLINE);
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
**Order of attack:** INSERT/BLOCK done; next the curve types (POLYLINE/ELLIPSE/SPLINE),
then fills (HATCH/SOLID), then block attributes (ATTRIB) + dims-in-blocks. **DWG export**
is two-stage lossy (Musa→DXF→converter→DWG); raising DXF-export fidelity (above) lifts it
too. (Native DXF export writes BLOCKS *after* ENTITIES — Musa's importer is order-agnostic;
strict BLOCKS-before-ENTITIES ordering for other readers is a minor noted item.)

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

**Status:** Settled — **DYN is now canvas-only by default** (Phase 27). ALL command
input is drawn on the GL canvas at the crosshair through ONE overlay primitive: command
entry + autocomplete (idle), mid-command sub-prompts (FILLET/CHAMFER/RECTANGLE
Dimensions/option keywords), and on-geometry dimension fields (rubber-band). **F12**
toggles canvas-only ⇄ the classic bottom command-line bar (the toggleable fallback; the
bar always returns on F12 / the status DYN toggle — no-stuck). Draw/transform commands
are **interactive** (ribbon starts the command, pick on screen). Command-input state
(prompt + history) lives in the `CommandProcessor`, so hiding the bar loses nothing; the
legacy cursor box is retired. One focus-routing path (app-wide event filter), one
suggestion source (the registry), one submit pipeline (`compose_dyn_submit` /
`submit_line`). The **ARRAY** multi-parameter dialog (Ph11 `ParameterDialog`) remains
for genuinely parametric commands. The command pipeline is untouched (collect-and-submit
only).

**Remaining:**

1. **More command options as keywords** (work in command line + DYN): CIRCLE 2P/3P/TTR;
   RECTANGLE area/rotation; ARC/ELLIPSE option sets. Each is a command state-machine
   keyword, surfaced via the mirrored prompt.
2. **POLYGON** — no POLYGON command exists yet (build the command + its sides/
   inscribed-circumscribed/radius options).
3. **Context-aware Dynamic Input — full canvas surfaces (DONE 2026-06-17)** —
   AutoCAD's DYN is not one cursor box but input drawn ON the canvas at the cursor.
   - **Done (all three surfaces, one primitive):** (a) **command entry + autocomplete**
     near the cursor when typing on an idle canvas; (b) **mid-command sub-prompts** at
     the cursor for any scalar/keyword step (FILLET radius, CHAMFER distances, RECTANGLE
     Dimensions length/width, Area, Rotation, option keywords); (c) **on-geometry
     dimension fields** during a rubber-band (RECTANGLE Length + Width on the two edges,
     LINE Length + Angle, CIRCLE Radius). All **drawn on the CANVAS** (the GL viewport
     overlay, NOT OS windows — the QWidget-tooltip attempt drifted off the geometry on
     multi-monitor; canvas rendering is glued by construction). You **type WITHOUT a
     click** (the app-wide event filter routes keystrokes), **Tab/Shift-Tab** switch
     dimension fields, **Enter** commits/advances, **Esc** cancels; a typed dimension
     **locks** while the cursor drives the others (render-side, zero op-log churn).
     Dimension fields come from the declarative per-command schema (`command::dyn_fields`
     over `PreviewKind`); scalar sub-steps set `PreviewSpec::scalar_prompt` so they route
     to the sub-prompt cell, not the field drag. Everything submits through the SAME
     pipeline as the command line (`compose_dyn_submit` / `submit_line`). **F12** toggles
     canvas-only ⇄ the classic bottom bar (state in `CommandProcessor`; no-stuck).
   - **Still deferred:** the dimension-field schema for the remaining draw/modify
     commands (ARC, POLYLINE, MOVE/ROTATE/SCALE, dimensions, …) — each a new `dyn_fields`
     case; **option chips / a Down-arrow dropdown** for bracketed `[Diameter]`/`[Area/
     Dimensions/Rotation]` keywords *in the cell* (today they are typed).
4. **A declarative command schema** so each command declares its fields/options
   *once* and gets the command-line prompts, the dialog, and dynamic input for
   free (avoid hand-writing each surface). `DialogSpec` is the seed of this.

**Done looks like:** every draw/modify command offers the AutoCAD-equivalent
dialog or dynamic-input field set, with live preview, all converging on the same
`Command` messages — no parallel pipeline.

Tracking rows live in [COMMANDS.md](COMMANDS.md) under "Command-line UX".
