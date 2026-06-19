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
| RECTANGLE options: **Dimensions** (`D` → length → width → quadrant-flip placement click), **Area** (`A` → area → `[Length/Width]` → side → placement), **Rotation** (`R` → angle) | typed mid-command | Implemented (option keywords, same state machine as CIRCLE `[Diameter]`) |
| RECTANGLE first-corner options: Chamfer / Elevation / Fillet / Thickness / Width | C/E/F/T/W | Planned |
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
| OFFSET (line/circle/arc) | O | Implemented |
| OFFSET (polyline, incl. closed rectangles + bulged/filleted corners) — each segment offset (lines parallel, arcs concentric with the bulge preserved) and **corners re-mitered** as the intersection of adjacent offset curves (line/line, line/arc, arc/arc via the shared line_line / line_circle / circle_circle primitives), so edges stay at distance d with clean corners (no trapezoid). Over-large offsets that would fold the shape fail gracefully ("Offset distance too large for this polyline.") leaving the geometry unchanged | O | Implemented |
| ROTATE | RO | Implemented |
| SCALE | SC | Implemented |
| ARRAY (rectangular + polar, command-line) | AR | Implemented |
| TRIM a line (cut by line/circle/arc edges) | TR | Implemented |
| TRIM an arc/circle/polyline *entity* | TR | Partial (line entities only; curve entities deferred) |
| EXTEND a line (to line/circle/arc boundary) | EX | Implemented |
| EXTEND an arc/polyline *entity* | EX | Partial (line entities only) |
| FILLET (line/line; radius 0 or tangent arc) | F | Implemented |
| FILLET (polyline corner → a true arc segment / bulge, dimensionable) | F | Implemented (incl. RECTANGLE corners — a rectangle IS a closed polyline; verified end-to-end through the full F-command path, including the closing-edge wrap corner) |
| Polyline arc segments (per-vertex bulge, AutoCAD LWPOLYLINE) | — | Implemented |
| FILLET (arc/curve cases) | F | Partial (line + polyline-corner only) |
| CHAMFER (line/line; Distance or Angle method, 45° default) | CHA | Implemented |
| CHAMFER (polyline corner) | CHA | Implemented |
| JOIN — **select** lines/arcs/open polylines (any way), then JOIN merges every connected chain among them into ONE polyline each (arcs become bulged segments), inheriting the source's layer/properties; a chain whose ends meet becomes a **closed** polyline (which then OFFSETs uniformly). The merged polyline is a single entity — moving it or a grip keeps it connected. With nothing pre-selected, JOIN falls back to picking a source + targets. One undo group | J | Implemented |
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
| **AutoCAD control codes in TEXT & MTEXT** — `%%d`→°, `%%p`→±, `%%c`→⌀, `%%%`→literal %, `%%nnn`→char by code, `%%o`/`%%u`→overline/underline toggles; MTEXT also `\U+XXXX`→Unicode. Codes are **stored raw and expanded at render time** (editing shows the raw codes; save/load + DXF round-trip them). Shared with the Leader/MLeader labels | (type in any text) | Implemented |
| **Leader / MLeader label properties in PR + MATCHPROP** — selecting a Leader or MLeader shows a Text section (Contents, Height, Font; MLeader also Width factor / Line spacing / Attachment); MATCHPROP copies the label font/height across the whole text family (TEXT ↔ MTEXT ↔ Leader ↔ MLeader) | PR / MA | Implemented |
| **Properties palette (PR): dockable, context-sensitive panel for the selection** | PR / PROPERTIES / PROPS / CH | Implemented (Ph22) |
| PR multiplicity: nothing / one / many-same / many-mixed, with **\*VARIES\*** where values differ; edits set all | — | Implemented (Ph22) |
| PR universal props: **Layer / Color / Linetype / Lineweight** (ByLayer or override) editable single + multi + mixed | — | Implemented (Ph22) |
| PR Geometry group (read-only): line length/ends, circle/arc center+radius, text position | — | Implemented (Ph22; numeric geometry editing Planned) |
| PR full **Text / MTEXT** group: contents, height, rotation, justify, width factor, line spacing, defined width, attachment, **font** | — | Implemented (Ph22; font dropdown real in Ph29) |
| **Font** dropdown (Standard stroke font + system TrueType/OpenType faces); switching re-renders the selected text as one undo group (varies/set-all) | PR Font | Implemented (Ph29) |
| Imported text fonts: TTF-by-name resolves to the installed face (filled glyphs); single-stroke SHX fonts (romans/simplex/isocp/txt…) render with the built-in single-stroke font (faithful match); missing → stroke fallback (true SHX binary parsing staged) | — | Implemented (Ph29) |
| PR deep **Dimension** group: per-dimension overrides — arrowhead type/size, dim-line & ext-line color, text height/color/placement, precision (each ByStyle or Overridden, with reset-to-style) | — | Implemented (Ph24) |
| Per-dimension overrides: resolve override-first-else-style (the Ph12 pattern) in compute_dim_geometry; one undo group; native round-trip; DXF override-vs-style distinction is native-only (stated gap) | — | Implemented (Ph24) |
| PR numeric **geometry editing** for line/circle/arc/polyline/leader | — | Planned (read-only display today) |
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
| **MATCHPROP / MA** — pick a source object (**or run MA with an object already selected → it becomes the source**, noun-verb), then pick destination object(s) (or **[Settings]**); each destination immediately adopts the source's properties via the Ph22 SetProperty descriptor path (the SAME write path the PR palette uses — no MA-specific entity-write code). **Universal** properties (colour / layer / lineweight / linetype — **ByLayer state preserved**, not the resolved literal) copy across ANY source/target kinds; **family-scoped** (text: height/font/justify/width-factor; dimension: per-dim overrides) copy only within a shared family; non-applicable properties are silently skipped. A **paintbrush cursor** shows while picking; **each matched target is its own undo entry** (undo in reverse). **Settings** (type `S`) opens a dark modal listing the categories (all on by default; persisted in QSettings for the session) | MA / PAINTER | Implemented |
| Off / Frozen layers skip rendering | — | Implemented |
| Locked layers (drawn, not selectable/modifiable) | — | Implemented |
| Linetype property (Continuous/Dashed/Center/Hidden) | — | Implemented (model + round-trip + **dashed rendering**, Ph23) |
| **Linetype pattern rendering** (dashes drawn on lines, polylines, and curves by arc-length; ByLayer + override) | — | Implemented (Ph23) |
| **LTSCALE** (drawing linetype scale, **per document**; prompts `<current>`, rejects ≤0; re-dashes live; native + DXF `$LTSCALE` round-trip) | LTSCALE / LTS | Implemented (Ph23) |
| **CELTSCALE** (per-entity linetype scale; PR "Linetype scale" field on line/circle/arc/polyline; MATCHPROP-matchable; native + DXF code 48 round-trip). Effective dash scale = **LTSCALE × CELTSCALE** | PR ▸ Linetype scale | Implemented |
| **Dynamic Input (canvas-only by default)** — all command input is drawn ON the canvas at the crosshair: command entry + autocomplete, mid-command sub-prompts, and on-geometry dimension fields. **F12 toggles** canvas-only ⇄ the classic bottom command-line bar (F12 ON = canvas, bar hidden; F12 OFF = bar visible, no canvas DYN). State persists; the bar always returns on F12 / the status DYN toggle (no-stuck fallback) | F12 / status DYN toggle | Implemented (Ph25–27; state persisted) |
| **DYN canvas command entry** — start typing on an idle canvas and a command-entry box appears at the cursor with the registry autocomplete dropdown (Down/Up/Tab select, Enter runs, Esc clears); routed by the app-wide event filter; bounded glyph batches regardless of suggestion count | — | Implemented (Ph27) |
| **DYN canvas sub-prompts** — any mid-command value/keyword step (FILLET radius, CHAMFER distances, RECTANGLE Dimensions length/width, Area, Rotation, option keywords) renders as an at-cursor prompt cell; type → Enter commits/advances, Esc cancels. Same primitive as entry + dimension fields | — | Implemented (Ph27) |
| DYN live dimensional input: type an exact length/angle (line), radius (circle), width/height (rectangle) during the rubber-band; Tab between fields | — | Implemented (Ph25; honors ORTHO/POLAR/snap) |
| **DYN on-geometry value fields** — value boxes drawn ON the canvas (in the GL viewport overlay, NOT OS windows) anchored to the rubber-band geometry, so they are always glued to it and cannot drift on multi-monitor (length under one edge, width by the other for RECTANGLE; length + angle for LINE; radius for CIRCLE), nudged just outside the edge so they never overlap. Type WITHOUT a click (the viewport captures dimension keystrokes); **Tab/Shift-Tab** switch fields; **Enter/Space** commit; **Esc** cancels; the mouse still drives the rubber-band. A typed value locks that dimension while the cursor drives the other(s) | F12 | Implemented (RECTANGLE/LINE/CIRCLE; other commands staged) |
| **DYN command-control keys (Esc / Enter / Space)** — with DYN on, these always reach the command (never swallowed by the on-canvas fields). **Esc** always cancels the active command — even with a half-typed value — clearing the rubber-band and fields. **Enter** / **Space** are AutoCAD's two-step: a pending typed value commits (keep drawing), otherwise the step ends (Enter at a LINE next-point prompt ends LINE). Tab/Shift-Tab still cycle fields. One carve-out in the app-wide event filter; F12-OFF (classic bar) unchanged | Esc / Enter / Space | Implemented |
| Single input surface per step: every step has exactly ONE place to type — the on-canvas entry (idle), sub-prompt cell (mid-command scalar/keyword), or dimension fields (rubber-band). The legacy cursor box is retired. State lives in the `CommandProcessor` (prompt + history), so hiding the bottom bar loses nothing; everything submits through the same pipeline (`compose_dyn_submit` / `submit_line`) | — | Implemented |
| **DYN autocomplete**: the Ph6 command-suggestion dropdown anchored at the cursor entry box (one suggestion source: the registry) | — | Implemented (Ph26–27) |
| Draw/transform ribbon buttons **start the interactive command** (pick on screen, like typing — never a fixed-position dialog): Line/Circle/Arc/Rectangle/Rotate/Scale | ribbon | Implemented (AutoCAD model) |
| **Starting a new command cancels the one in progress** — clicking a ribbon command (or otherwise dispatching a new command) while one is active cleanly cancels the current command first (its rubber-band/preview is dropped) and starts the new one; the **selection is preserved**. One dispatch site (`CommandProcessor::start_command`) | ribbon / dispatch | Implemented |
| **CIRCLE radius/[Diameter] option** — type `D` at the radius prompt (command line or DYN) to enter a diameter instead | C → D | Implemented (Ph26) |
| Parametric **multi-parameter dialog** (collect values → submit existing Command): ARRAY | ribbon | Implemented (Ph11) |
| POLYGON command + dialog | — | Planned (no POLYGON command yet) |
| **Import DWG** — runs an external converter (DWG→DXF) off-thread, then the existing DXF importer (fail-safe); writes a `<file>.dwg.import.log` gap catalog | ribbon / DWGIN | Implemented (Ph27; needs an installed converter — ODA File Converter or LibreDWG) |
| **Export DWG** — existing DXF export, then the external converter (DXF→DWG, default ACAD2018) | ribbon / DWGOUT | Implemented (Ph27; two-stage lossy, see ARCHITECTURE) |
| **DWG Setup** dialog — detect/Browse/auto-detect the converter, links to downloads; saves the path setting (offered via "Configure…" when none is found) | ribbon "DWG Setup" | Implemented (Ph27) |
| DWG converter path (configurable) | `io/dwg_converter_path` setting / DWG Setup dialog | Implemented (Ph27; auto-detects ODA/LibreDWG on PATH otherwise) |
| Per-entity linetype scale (CELTSCALE) | — | Planned |
| Lineweight property (hundredths-mm) | — | Implemented (model + round-trip; visible weight Planned) |
| LAYER command-line alias / PROPERTIES palette / MATCHPROP | LA / PR / MA | Planned (Phase 13) |

## Blocks / Reference

Block **definitions** + **INSERT** references (transform-at-snapshot, not exploded). This
phase covers **import, display, and selection**; in-app authoring is staged.

| Command | Alias | Status |
|---|---|---|
| Block import (BLOCKS + INSERT, nested, scale/rotation) from DWG/DXF | File ▸ Import | Implemented (Ph28) |
| Display block instances (definition × transform, resolved at snapshot; per-instance colour/layer; batched — N instances add no draw calls) | — | Implemented (Ph28) |
| Select / hover / window-crossing a block as one object; move / erase / copy the instance (not the definition); insertion-point grip; INS osnap | click / grips / Modify | Implemented (Ph28) |
| BLOCK / WBLOCK — define a block from selected geometry | B | Staged (authoring half) |
| REFEDIT — edit a definition; all instances update | — | Staged |
| EXPLODE — instance → its geometry | X | Staged |
| ATTDEF / ATTRIB — block attribute text | ATT | Staged |
| XREF | XR | Planned |

## File / Plot

| Command | Alias | Status |
|---|---|---|
| Native format v5 (adds polyline arc bulges) | — | Implemented (v1–v4 load) |
| DXF LWPOLYLINE bulges (code 42, read/write) | — | Implemented (LibreCAD-verified) |
| DXF TEXT + DIMENSION (all subtypes) + LEADER + DIMSTYLE table | — | Implemented (leader label imports as separate TEXT) |
| LWDISPLAY (lineweight display on/off) | LWT ribbon toggle | Implemented |
| Lineweight display: DPI-anchored `px = mm × DPI/25.4`, zoom-independent, Default = 1px hairline (AutoCAD-accurate) | — | Implemented |
| NEW — opens a new untitled drawing in a **new tab** (existing tabs untouched) | Ctrl+N / "+" tab button | Implemented |
| OPEN (native .musa) — **always opens into a new tab**; unsaved work in the current tab is never overwritten | Ctrl+O | Implemented |
| SAVE / SAVE AS — operate on the **active** document (its own path) | Ctrl+S / Ctrl+Shift+S | Implemented |
| **Multiple documents as tabs** — N drawings open at once; each tab shows its name + a `*` dirty marker. ONE engine swaps the active document on switch; ONE viewport renders the active document's snapshot | tab strip | Implemented (Phase A) |
| **Switch tab** — click a tab, or cycle with Ctrl+Tab / Ctrl+Shift+Tab. Each tab restores its own camera (zoom/pan), selection, and undo/redo history | Ctrl+Tab / Ctrl+Shift+Tab | Implemented |
| **Close tab** — the × on each tab, or Ctrl+W. A dirty tab prompts Save / Discard / Cancel; closing the last tab leaves one empty "DrawingN" (never zero tabs) | × / Ctrl+W | Implemented |
| **Undo/redo is per document** — Ctrl+Z on tab A rewinds tab A's last op even after edits in tab B | Ctrl+Z / Ctrl+Y | Implemented |
| **Quit guard** — closing the window prompts to save every dirty document (Cancel aborts the quit) | — | Implemented |
| **Cross-document copy/paste** — Ctrl+C / Ctrl+X copy/cut the selection to an in-process clipboard; Ctrl+V pastes into the ACTIVE document at the cursor, remapping layer/dimstyle/block references by NAME (creating any missing in the target). One undo group; the clipboard survives switching/closing the source | Ctrl+C / Ctrl+X / Ctrl+V | Implemented (Phase B) |
| **Tab-to-tab drag** — drag a selection onto another document's tab to transfer it there (copy → switch → paste, original coordinates) | drag to tab | Implemented (Phase B) |
| DXF export (R2000 / AC1015; LAYER table + ByLayer colour 256) | File ▸ Export DXF | Implemented |
| DXF import (LINE/LWPOLYLINE/CIRCLE/ARC/POINT/TEXT/MTEXT/DIMENSION/LEADER; BLOCK defs + INSERT refs; reads the LAYER table + ACI colours) | File ▸ Import DXF | Implemented |
| DXF import (SPLINE / legacy POLYLINE) | — | Planned (skipped + reported for now) |
| Dirty tracking (modified `*` in title, prompt before discard) | — | Implemented |
| PLOT / PRINT (PDF + installed printers; paper/orientation/area Display·Extents·Window/scale fit·ratio/centre·offset/lineweights/CTB None·Mono·Grayscale/copies; window-pick; print-preview; off-thread; vector output) | Ctrl+P / PLOT / PRINT | Implemented (Phase 30) |
| Saved page setups (named, persisted in the drawing; recall in the PLOT dialog) | PLOT ▸ Page setup | Implemented (Phase 30) |

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
| Dynamic input tooltips at cursor | Implemented (full canvas DYN: command entry + autocomplete, mid-command sub-prompts, on-geometry dimension fields for RECTANGLE/LINE/CIRCLE; F12 toggles canvas-only ⇄ classic bottom bar) |
