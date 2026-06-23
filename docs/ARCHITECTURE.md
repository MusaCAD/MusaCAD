# Musa CAD — Architecture

This document tracks the system's structure as it is built phase by phase.
Sections marked _(planned)_ describe seams that exist now but are filled in
later phases.

## Modules / targets

| Target            | Kind       | Responsibility |
|-------------------|------------|----------------|
| `musacad_core`    | static lib | Math, generational handles, SoA `GeometryStore`, `IGeometryKernel` + `NativeKernel2D`, threading primitives, snapshots, spatial index. Warnings are errors here. |
| `musacad_render`  | static lib | Backend-agnostic RAII GPU abstraction (OpenGL 4.6 backend) + camera, grid, viewport renderer consuming snapshots. |
| `musacad_command` | static lib | Table-driven alias parser, per-command state machines, coordinate parsing, REPL/history. |
| `musacad_ui`      | static lib | Qt6 frame: ribbon, viewport host, command line, status bar, crosshairs. |
| `musacad_app`     | executable | `main()`, thread orchestration. |

`musacad_core` has **no dependency** on render/command/ui/Qt — it is the
headless heart of the engine.

## Threading model

Three long-lived threads communicate only via message queues and immutable
snapshots — never shared mutable state:

```
 UI / Command thread          Geometry / Compute thread        Render thread
 (Qt event loop, input)  ───▶ (owns SoA GeometryStore,    ───▶ (owns GPU device/
        Command msgs           applies commands, runs          swapchain, draws
        on MPSC queue          IGeometryKernel, builds         latest snapshot
                               render snapshots)               lock-free)
```

* **UI→Geometry:** `MpscQueue<Command>` — mutex + `condition_variable_any` with
  `wait_pop(stop_token)`. The command rate is low and bursty, so a lock here is
  the right tool: simple and sanitizer-clean. _(core, Phase 2)_
* **Geometry→Render:** `TripleBuffer<RenderSnapshot>` — lock-free SPSC; the
  renderer never stalls compute (see below). _(core, Phase 2)_
* `GeometryEngine` owns the store, kernel, queue and triple buffer, and runs the
  geometry worker on a `std::jthread` + `std::stop_token`. Stop is clean: the
  stop token wakes `wait_pop`, the loop exits, and the destructor joins. No
  detached threads, no manual join races. _(core, Phase 2)_

## Multi-document model (Phase A)

N drawings are open at once, but there is still **ONE engine, ONE viewport, ONE renderer,
ONE command queue, ONE snapshot triple buffer** — multi-document is a layer *above* the
threading model, not a fork of it.

**Per-document vs global.** Per-document state (the `GeometryStore`, spatial index,
undo/redo op-log, selection set, dirty flag, document version, pending-dim + grip-drag
state) is bundled in `GeometryEngine::DocState`. Global state stays on the engine: the
kernel, the MPSC command queue, the snapshot triple buffer, the worker thread, view-scale
/ tessellation buckets, input state (cursor/pick/osnap), and the status feedback channel.
The **camera is per-document** but lives UI-side (a `docId → Camera2D` map in the
viewport), since panning/zooming is pure CPU and never touches the geometry thread.
**LWDISPLAY** is treated as global (app-wide), matching AutoCAD.

**The "DocumentManager" is the engine's document registry, on the geometry thread.** The
prompt's notion of a global DocumentManager is realised here as the engine owning the
documents — because the engine owns the stores, and a UI-thread manager owning stores
would violate the "UI never touches the store" invariant the codebase has held since
Phase 2. The engine keeps the **active** document's heavy state in its live members (so
all single-document code is unchanged) and **parks** inactive documents' state in a
`docId → DocState` map. `GeometryStore`/`SpatialGrid` are movable, so a switch is a
move-swap of those members — no copy, no churn at the ~200 `store_` call sites.

**One engine, swappable active document.** Tab actions are geometry-thread commands:
`SwitchDocumentCommand{id}` parks the active doc and unparks the target (the next
snapshot is built from the new store); `CreateDocumentCommand` / `CloseDocumentCommand`
add/remove documents; `OpenDocumentCommand` gains a `new_tab` flag (the UI always opens
into a new tab). `NewDocumentCommand` still means "reset the active document in place"
(internal/test resets). The cross-document snapshot transition is **tear-free for free**:
the triple buffer doesn't care which document a snapshot was built from — a snapshot built
for doc A renders for doc A, and the next published one is doc B's.

**The UI is a pure view of the snapshot.** `RenderSnapshot` carries `documents`
(`{id, name, path, dirty}` per open doc, in tab order) + `active_document_id`; the
`FileTabs` strip, the window title, and Save's target path are all derived from it. The
viewport caches that list for the GUI thread; clicking a tab / Ctrl+Tab submits a
`SwitchDocumentCommand` (cancel-on-switch: an in-flight command is cancelled). Closing a
dirty tab (or quitting with dirty tabs) prompts Save/Discard/Cancel and flushes the queued
saves before teardown; closing the last tab resets it to an empty drawing (never zero
tabs).

**Cross-document clipboard (Phase B).** Copy/Cut/Paste (Ctrl+C/X/V) move entities BETWEEN
documents. The clipboard is an engine-global, geometry-thread-owned buffer (NOT parked
per-document, so it survives switching/closing the source) that snapshots the captured
entities PLUS the source document's named tables (layers, dimstyles, block defs). Paste
recreates into the active document, remapping every layer/dimstyle/block reference by
NAME via the store's get-or-add accessors (the same `add_layer`/`add_dimstyle`/`add_block`
pattern DXF import uses), creating any missing in the target; pasting an INSERT deep-copies
its block-definition closure (nested inserts + block-internal layers remapped). All on the
geometry thread, one undo group, the UI never touches a store. Layer/dimstyle/block AND
font references are all remapped (top-level text fonts travel as names; MLeader + block-
internal fonts remap via a snapshotted source font table). A self-referential or cyclic
block definition (malformed input) is broken with an in-progress guard rather than
recursing forever. Tab-to-tab drag is the same path: dragging a selection onto another
document's tab does copy → switch → paste.

### Snapshot handoff: triple buffer with an atomic latest-ready index

**Decision.** The geometry→render handoff uses a **lock-free single-producer /
single-consumer triple buffer** (`include/musacad/core/threading/triple_buffer.hpp`),
not a seqlock.

**Why triple buffer over seqlock.** The read/write rates are sharply asymmetric:
the render thread reads continuously at display refresh (144Hz+), while geometry
writes are infrequent and bursty (the user edits occasionally). A seqlock makes
the *reader* retry whenever it races a write; with a large snapshot payload a
retry means re-reading everything. A triple buffer instead gives the reader its
own buffer that the writer never touches, so the reader **never retries and
never blocks the writer** — it always has a complete, internally-consistent
snapshot. The cost is one extra buffer (three instead of one), which is cheap
and exactly the trade we want for a renderer that must not stall compute.

**Mechanism.** Three buffers and three roles — `write_` (producer-owned),
`read_` (consumer-owned), and the index packed into an atomic `shared_` (low 2
bits = index, bit 2 = "fresh"). The three indices `{0,1,2}` are always a
permutation across these roles. `publish()` exchanges the producer's index into
`shared_` and takes back whatever index was there; `acquire()` (only if the
fresh bit is set) exchanges the consumer's index into `shared_` and takes the
freshly published one. Because the operation is an *exchange*, the partition
stays a permutation, so the producer's current write buffer is never the
consumer's current read buffer — no two threads ever touch the same buffer.

**Memory ordering (correctness argument).**
* `read_`/`write_` are touched only by their owning thread, so they need no
  synchronization.
* The producer writes `buffers[i]` and then publishes index `i` with a
  **release** exchange (`memory_order_acq_rel`). Those writes are
  sequenced-before the release.
* The consumer reads `shared_` with **acquire**, and if fresh, performs an
  **acquire** exchange that observes the value `i` the producer released. That
  acquire **synchronizes-with** the producer's release, so every write the
  producer made to `buffers[i]` *happens-before* the consumer reads it.
* Therefore the consumer can never observe a half-written buffer (no tearing)
  and never an index that aliases the producer's live write slot.

**Verification.** `tests/test_triple_buffer.cpp` runs a writer publishing
200,000 monotonically-versioned snapshots in a tight loop (rebuilding the
payload from scratch each time) against a reader that asserts, on every
observed snapshot, that an embedded checksum matches the payload it guards and
that versions never go backwards. It passes under both ASan/UBSan (`dev`) and
ThreadSanitizer (`tsan`). `tests/test_geometry_engine.cpp` exercises the same
handoff plus the MPSC queue end-to-end with multiple concurrent producers.

## Geometry kernel strategy

Musa CAD owns its data model and a narrow kernel interface, and ships a
complete native 2D kernel. No third-party geometry kernel is a dependency.

* `IGeometryKernel` _(core, Phase 2)_ — curve evaluation, intersection,
  offset, tessellation, future booleans. **All inputs/outputs use our own
  types** (`Vec2`/`Vec3`, generational handles, SoA buffers). No external
  library type ever appears in a public `musacad/` header.
* `NativeKernel2D` _(core, Phase 2)_ — the default, fully-functional backend
  for all 2D primitives, operating directly on the SoA `GeometryStore`.
* A documented seam is left for a future `NativeKernel3D` / optional external
  B-rep backend behind the same interface — no such dependency exists now.

**Kernel surface (deliberately minimal).** `IGeometryKernel` exposes exactly the
three operations with a concrete caller in the current roadmap:
* `tessellate` — curve → polyline approximation; used by the renderer (Phase 3)
  and command echo (Phase 4).
* `closest_point` — OSNAP "nearest" (Phase 5).
* `intersect` — OSNAP "intersection" (Phase 5).

No `offset`, boolean, or parametric-eval methods are reserved: nothing in the
Phase 2–5 roadmap calls them (there is no OFFSET command), so per the
"no speculative surface" rule they are omitted until a caller exists. The seam
is the *type boundary* (our types in, our types out), enforced by both a
compile-time check (`tests/test_header_hygiene.cpp`, which includes every public
core header while linking only `musacad_core`) and a textual scan
(`tests/header_scan.cmake`).

### Extension point: a future 3D / B-rep backend

To add 3D solids later, implement `IGeometryKernel` in a new `NativeKernel3D`
(or an adapter over an external B-rep kernel) and add 3D primitive arenas to the
store. Crucially, any B-rep/tessellation results are converted to **our** SoA
`Vec2`/`Vec3` buffers at the kernel boundary, so the command, render, and UI
layers — which only ever see `RenderSnapshot` and our types — need no changes.
No external library type may cross into a public `musacad/` header.

## Data-oriented storage

Geometry lives in contiguous Structure-of-Arrays `std::vector` storage indexed
by generational handles, not scattered polymorphic objects. Each primitive kind
(`Point`, `Line`, `Polyline`, `Circle`, `Arc`, `Spline`) has its own
`GenerationalArena<T>`; variable-length primitives (polyline, spline) store an
`(offset, count)` view into a shared vertex pool. _(core, Phase 2)_

A slot's generation encodes liveness by **parity**: even = free, odd = live.
Inserting bumps it to odd (reusing a free-list slot when available); erasing
bumps it to even and returns the index to the free list. A handle is valid only
while its stored odd generation matches the slot, so erasing one entity never
invalidates handles to others, and a reused index is detectably distinct from
the stale handle that previously held it. Batch consumers iterate the raw span
and skip dead slots via `alive(i)`. No virtual dispatch is used on these paths.

Insertion throughput (release build, this machine): **~32 ns/line**, i.e. 1,000,000
lines in ~32 ms.

### Blocks: definition table + INSERT references (Phase 28)

A **block definition** is a named, self-contained collection of geometry held in a
block-definition table on the store (parallel to the layer table) — deliberately
**outside** the model-space arenas, so a definition never appears in the snapshot, pick,
or `all_live` on its own. Model space holds lightweight **`InsertData`** entities (their
own arena + generational handle, like every other kind): a definition index + a transform
(insertion point, X/Y scale, rotation). 10 instances of a block are 10 small inserts, not
10 geometry copies.

An insert's drawable geometry is **resolved at snapshot from definition × transform, never
baked** (the same parametric, derived-not-baked rule as dimensions, Ph16/23). One function —
`resolve_insert` (`block_resolve.cpp`) — produces the world-space segments, composing
`translate · rotate · scale`; nested inserts compose `parent × child` with a depth guard
(cap 16) against cyclic definitions. That single path feeds **every** consumer — the render
snapshot, entity bounds (AABB / window-select / zoom-extents), the kernel's `tessellate`
(window/crossing/hover) and `closest_point` (pick) — so the displayed, picked, and bounded
geometry can never diverge. Inserts emit into the existing colour/lineweight line batches,
so N identical instances add vertices but **no extra draw calls**.

Like dimensions, an insert resolves to **disjoint segment pairs** (each primitive its own
run, no phantom connectors); `closest_point` and the window/hover consumers step those
pairs. Clicking anywhere on the instance's geometry selects the **whole insert** (one
handle), and move/erase/copy act on the instance's transform — the shared definition is
untouched. In-app block authoring (BLOCK/REFEDIT/EXPLODE/ATTDEF) is staged for a later
phase; this phase is import + display + selection.

### Text: two render paths behind one layout (Phase 29)

Text has **two coexisting geometry sources**, selected per entity by a font reference:

- **Stroke font** ("Standard", index 0) → **LINE** geometry through the line pipeline.
  The built-in single-stroke CAD font; the default and the fallback. Qt-free.
- **TrueType/OpenType** faces → **FILLED triangles** through the Phase-14 fill pipeline.
  Glyph outlines come from Qt (`QRawFont`/`QPainterPath`), triangulated (ear-clip) once
  per glyph and cached at unit em; N glyphs of one colour batch into **one draw call**.

Only the **per-glyph geometry source** differs — wrap, attachment, line-spacing,
justification, and rotation are **one shared layout** (`layout_mtext`), and the width
metric is shared too (stroke `text_width` vs the face's advance), so layout, bounds, and
pick never diverge from what is drawn. Glyphs are **generated at snapshot from (font,
string, height), never baked** (the same derived-not-baked rule as dimensions/blocks):
switching font or editing text re-generates next snapshot.

Core stays **Qt-free behind an interface**: `IFontEngine` (mirrors `IGeometryKernel`) is
implemented by `QtFontEngine` in the UI layer and injected via the store
(`store.font_engine()`), so the snapshot, entity bounds, pick, and grips all reach the
same metrics. Font **enumeration** runs once on the UI thread (`QFontDatabase`); glyph
**outline extraction** runs lazily on the geometry thread on a cache miss (CPU-only,
mutex-guarded, safe off the GUI thread); steady state is Qt-free cache lookups.

A text entity stores a **font-table index** (`std::uint16_t`, 0 = stroke) — compact, no
inline string on the hot path. **Import substitution**: a `*.shx` reference (single-stroke
CAD shape fonts: `txt`, `simplex`, `romans`, `isocp`, …) renders with the built-in
single-stroke font — its faithful match (a filled TTF looks wrong) — while a TTF/OTF family
name (or `*.ttf`) resolves to that installed face (or a Liberation/DejaVu equivalent), drawn
as filled glyphs. An unresolved name falls back to the stroke font; nothing is ever blank.
True SHX *binary* parsing is staged.

Glyph fill geometry is produced by a **scanline rasterizer-into-triangles** (even-odd spans
at fine horizontal bands → quads), which is robust to any number of counters/holes — no
ear-clipping corner cases. The triangles are cached per (face, glyph) at unit em.

### Text control codes: render-time substitution (derived-not-baked)

AutoCAD control codes (`%%d`→°, `%%p`→±, `%%c`→⌀, `%%%`→`%`, `%%nnn`→Latin-1 char, the
`%%o`/`%%u` over/under-line toggles, and the MTEXT `\U+XXXX` Unicode escape) are expanded
by **one function**, `core::text::substitute_text_codes`, **at render/layout/measure time** —
the entity keeps the **raw** string (the same derived-not-baked rule as dimensions/blocks).
So editing re-shows `%%c50`, and native + DXF round-trip the codes byte-for-byte (DXF import
no longer bakes them — `strip_mtext` even preserves `\U+`). The pass returns the visible
UTF-8 string plus the over/under-line runs; it is called at the three "raw string → layout"
seams — `text_advance` (single-line measure), `layout_mtext` (paragraph measure+emit), and
the single-line `emit_text_run` — so measurement, bounds, and glyphs never diverge.
Over/underline are drawn as a horizontal stroke spanning each toggled run (single-line
today; the symbol codes work in both TEXT and MTEXT). The stroke font carries °, ±, ⌀ as
glyphs; a TTF face supplies them when selected.

### Leader / MLeader labels are text-family (no new family)

`family_of` already groups `Text`, `MText`, `Leader`, and `MLeader` into
`EntityFamily::Text`, so MATCHPROP's family gate already lets label properties travel across
the whole text family. Exposing them in the PR registry was therefore **not** a new family —
it was broadening the descriptors' `applies` predicate (`is_text_family` = the four kinds;
`is_paragraph` = the MTextBlock-bearing MText + MLeader) and letting the existing
`requires`-based accessors reach the label fields (`get_height` gained an `x.text_height`
branch for the flat LEADER). The one structural change: `AddMLeaderCommand` gained a
top-level `font` **name** (it previously carried only the resolved `block.font` index),
captured/applied/clipboard-remapped exactly like MTEXT, so the font descriptor and MATCHPROP
read/write it as a name. A separate `MLeaderFamily` was explicitly **rejected** — it would
have broken the existing TEXT↔MLeader matching.

The **leader arrow** is governed by the leader's referenced dimstyle, so per-leader
editability reuses the dimension **override** machinery rather than new storage primitives:
`LeaderData`/`MLeaderData` gained a `DimOverrides` field (only the arrow bits are meaningful;
they are annotation entities, not hot like Line/Circle, so the ~40 B is inline like
`DimData`). The render resolves `apply_dim_overrides(dimstyle, leader.overrides)` at the
arrowhead (override-first-else-style). The PR descriptors `LeaderArrowType`/`LeaderArrowSize`
(group "Leader") reuse the *same* read/write lambdas as the dimension arrow descriptors — the
`requires`-based `get_dim_ov`/`with_dim_ov`/`get_dim_style` accessors work once the leader
commands carry `overrides` + a resolved `dim_style` snapshot (for the ByStyle display). They
match `leader↔leader` under `MatchSlot::Text` (leaders are text-family). Native bumps to v13
(the override block is appended to LEADER/MLEADER records, detected by token count, older
files ⇒ ByStyle); DXF is effective-value-only for the override, the same documented gap as
dimension overrides.

The leader **label colour** is a fourth descriptor (`LeaderTextColor`) on the same override
field — `DimOverrides::kTextColor` already exists and the v13 block already serialises all
three element colours, and the render already does `text_c = s.text_color.resolve(r.color)`
after `apply_dim_overrides`. So it needed *only* the descriptor (reusing the `DimTextColor`
read/write lambdas): no data-model, render, or persistence change. ByStyle ⇒ the leader's
entity colour; an override colours the label independently of the leader line + arrow (which
follow the General colour).

### Stroke-text quality: on-screen weight, real lowercase, edge AA (Phase 31)

Single-stroke text stays the engineering default ("Standard"); three changes make it look
professional **on screen** without changing what reaches paper:

- **Screen-only text weight (one render path, a policy parameter).** Stroke text carries no
  real lineweight (it is emitted at `lineweight = 0`), so it used to draw as a 1 px hairline
  that read as "dull" — sub-pixel thin on HiDPI. The snapshot now tags those batches with
  `ColorBatch::is_text` (derived at build, not baked, not in the checksum; only ever set on
  `line_batches` — filled TTF lives in `fill_batches`). The **viewport** renderer draws
  `is_text` batches at a ~0.5 mm-equivalent screen weight (`kTextScreenWeightHmm`) through
  the *same* thick-line pipeline — no second code path, no extra geometry. That weight is a
  **cap**: each text batch carries its quantised cap height (`ColorBatch::text_height`, keyed
  into the batch), and the renderer tapers the stroke to a fraction of the glyph's *on-screen*
  height (`text_height × camera.scale()`), floored at a 1 px hairline — so tiny title-block
  fields read crisp instead of blocky while title-size text keeps full presence. The
  **plot** path is a separate route (QPainter) that ignores `is_text` and honours the entity
  weight (0 → cosmetic hairline), so the on-screen polish **never reaches paper**: a plot of
  the same drawing is ink-for-ink identical before/after (verified: 0-pixel raster diff). This
  is the "same entity, two render contexts" pattern (cf. screen vs plot lineweight).
- **Real lowercase.** The built-in stroke font (`core::text::stroke_font`) gained true
  lowercase a–z (simplex/Hershey-class, hand-authored on the existing 6×8 cell with proper
  ascenders/x-height/descenders), replacing the Phase-13 small-caps fallback (kept only as a
  defensive path for a missing glyph). The font stays **monospace** (one shared advance), so
  `text_width`, layout, bounds, and pick are unchanged. SHX→stroke substitution still routes
  here, so every imported `*.shx` drawing benefits automatically; the plot shows the improved
  letterforms too (ink *weight* unchanged — a font-quality improvement, not a weight change).
- **Antialiased edges.** `thickline.frag` replaced its hard `discard` with an analytic ~1 px
  coverage feather (interior stays fully opaque; only the capsule boundary fades), and the GL
  backend enables standard alpha blending. The Phase-16B round caps/joins are preserved. This
  applies to all thick-line geometry, not just text; all theme colours are opaque so interiors
  are unaffected. The plot path is QPainter and unaffected.

## Rendering (Phase 3)

### GPU backend: OpenGL 4.6 Core (DSA), behind a backend-agnostic seam

The renderer talks only to a small RAII abstraction — `GpuDevice`, `GpuBuffer`,
`GpuPipeline`, `GpuCommandBuffer`, `GpuRenderTarget` (the public
`include/musacad/render/gpu/` headers, free of any GL/Qt type). The only backend
is OpenGL 4.6 Core using Direct State Access, implemented privately under
`src/render/gl/`.

Why OpenGL rather than Vulkan, here and now:
* It runs and is **measurable in this environment** — a real GL 4.6 context
  (Intel UHD 630 / Mesa, and NVIDIA via EGL) is available; Vulkan exposes no
  usable device here and the SPIR-V toolchain (`glslc`) is absent.
* Qt supplies the window surface and the GL function loader
  (`QOpenGLFunctions_4_5_Core`), so there is **no GLAD/GLEW/Vulkan-SDK
  dependency** — the project stays clone-and-build clean.
* DSA (`glCreate*`/`glNamed*`) gives an explicit, Vulkan-like resource model, so
  a future Vulkan backend implements the same interface without touching the
  renderer, camera, command, or UI layers.

Tradeoff: GL keeps implicit global state; we contain it in the RAII wrappers and
an immediate-mode `GpuCommandBuffer` (the GL backend issues calls as recorded
with the context current; a Vulkan backend would defer them — the begin/record/
submit shape supports both).

### Render thread and the snapshot

`ViewportWindow` (a `QWindow`) runs a dedicated render thread (`std::jthread`)
that exclusively owns the GL context. Each frame it consumes the latest snapshot
from the existing `GeometryEngine` triple buffer (lock-free; **no second handoff
path**) and draws it. It never touches the `GeometryStore`. Camera input is
handled on the GUI thread and shared under a small lock; the `Camera2D`
transform is pure CPU, so pan/zoom never involve the geometry thread.

### Instanced drawing

Lines and points are drawn **instanced**: one instance per primitive, with
per-instance attributes (a `vec4` segment, or a `vec2` point) sourced from
snapshot-derived buffers; base geometry comes from `gl_VertexID`. The entire
scene — grid (minor+major), all lines, all points, and the overlay — draws in
**~4 draw calls regardless of primitive count**. Scene instance buffers are
re-uploaded only when the snapshot **version** changes, so pan/zoom upload zero
scene bytes.

### Live-geometry snapshot (constraint A)

`build_render_snapshot` (in `core/`) visits only live arena slots and their live
`(offset,count)` vertex views, so deleted polylines/splines leave no residue
even though the vertex pools are append-only. Verified at the data level
(`test_scene_snapshot.cpp`) and through the real renderer
(`tests/render_offscreen.cpp`): adding then deleting 50k polylines returns the
uploaded instance count to baseline.

### Measured (real hardware, this machine)

Offscreen render of a **1,000,000-line** scene on the Intel UHD 630 (Mesa),
1280×720, no vsync: **4 draw calls**, **~2.1–2.4 ms/frame (≈420–470 FPS)**, and
**0 bytes** of scene re-upload across 300 pan/zoom frames (constraint B). The
onscreen app runs the render thread against the live Wayland compositor; its
sustained on-screen FPS is vsync-limited and not separately benchmarked here.
The shader build step embeds GLSL into a generated header at configure/build
time (`cmake/EmbedShaders.cmake`) — a clean build needs no manual shader step.

## Command line & input (Phase 4)

### Pipeline and the threading boundary

```
 keystrokes ─▶ CommandLineWidget ─▶ CommandProcessor ─▶ ICommand state machine
   (UI thread)                          │ emits core::Command
                                        ▼
                              UI→geometry MPSC queue ─▶ GeometryEngine (geometry thread)
                                        │ applies + records undo
                                        ▼
                              RenderSnapshot (triple buffer) ─▶ render thread
```

Everything left of the queue runs on the UI thread; the **only** thing crossing
to the geometry thread is a `core::Command` message on the existing MPSC queue.
The UI thread never reads or mutates the `GeometryStore`. The live
world-coordinate readout in the status bar uses the render/UI-side `Camera2D`
transform (`screen_to_world`) — no geometry round-trip. `ZOOM` likewise drives
the viewport camera (extents uses the snapshot's `bounds_*`), never the geometry
thread.

### Parser is data; behaviour is a state machine

`CommandRegistry` is a pure alias→factory table (`command_registry.cpp`); adding
a command is one `register_command({...}, factory)` row. Each command
(`LineCommand`, `CircleCommand`, …) is its own small `ICommand` state machine
that prompts, parses input, and emits messages. Parsing and command behaviour
never share a switch. Coordinate parsing (`coordinate.cpp`) handles absolute
`x,y`, relative `@dx,dy`, and polar `@dist<angle`; malformed input is echoed and
re-prompted rather than aborting the command.

### Undo model (geometry thread)

Undo lives entirely on the geometry thread, which owns the store — there is no
UI-side shadow geometry. The engine keeps an **op log**: each entity-creating
command pushes a `Created` op (the handle to remove on undo); `ERASE` pushes
`Erased` ops capturing an `Add*` command that recreates the geometry. Every op
carries the `group` id of the command-line invocation that produced it (all
segments of one `LINE` share a group).

* Command-line `U` → `UndoLastGroupCommand`: pops the whole most-recent group
  (undoes an entire `LINE`/`CIRCLE`/`ERASE` invocation).
* In-command `[Undo]` → `UndoLastOpCommand`: pops a single op (e.g. the last
  `LINE` segment while still drawing).

Undo of a creation removes the entity; undo of an erase re-adds it (with a fresh
handle). This is intentionally the smallest undo that covers the Phase-4
commands — no general transaction system. Limitation: an entity restored by
undoing an erase is not re-tracked for `ERASE Last`/group undo (acceptable for
the current command set).

## UI frame, snapping & shortcuts (Phase 5)

### One spatial index, two consumers

`SpatialGrid` (`core/spatial_grid.*`) is a uniform grid bucketing entity AABBs.
It is built and maintained **geometry-side** by the `GeometryEngine` in lockstep
with the store and undo/redo: `create_indexed` inserts, `remove_indexed` removes,
and undo/redo recreate or remove through the same paths, so the index always
matches live geometry. It is the single proximity structure feeding **both** the
OSNAP engine and ERASE cursor-pick (`pick_nearest`). The store insert baseline is
unchanged (~32–35 ns/line); index maintenance adds ~230 ns/entity (engine-side,
hash-bucketed) — a uniform grid with a fixed cell size, adaptive sizing left as a
future optimization.

### Snapping rides the existing snapshot (constraint A)

There is **no synchronous per-move geometry query and no second handoff**. The
cursor reaches the geometry thread as a coalesced, non-blocking `SetCursorCommand`
on the existing MPSC queue (mouse-move floods collapse into one rebuild). The
geometry thread computes the best snap (`compute_snap`: index query → exact
endpoint/midpoint/center/intersection/nearest tests, priority-ordered) and writes
it into the **existing `RenderSnapshot`** (`has_snap`/`snap_point`/`snap_type`),
which the render thread already consumes lock-free. To keep cursor-only updates
cheap, the engine caches the tessellated geometry payload and rebuilds it only
when geometry changes (`geom_dirty_`); cursor frames copy the cache and refresh
only the marker.

Crosshairs are **pure render-side**: the raw cursor is shared GUI→render via
atomics and drawn every frame in screen space, so they stay smooth regardless of
snap load or scene size. The whole interaction layer adds a bounded **2 draw
calls** (crosshair + one marker): 4 for the scene, 6 with aids — never
per-primitive.

### Redo (constraint C)

The Phase-4 op log became symmetric undo/redo **group** stacks. Each command-line
invocation is one group of `Item{recreate-command, live-handle}`; a *create*
group's undo removes and redo recreates, an *erase* group's undo recreates and
redo removes. A new edit clears the redo stack. This is the smallest extension
that redoes the existing commands — not a general transaction system. `Ctrl+Z`
→ `UndoLastGroupCommand`, `Ctrl+Y` → `RedoLastGroupCommand`.

### Drawing aids and shortcuts

Ortho (H/V) and polar (45° increments) constrain *free cursor picks* only (typed
coordinates stay exact), resolved UI-side in `CommandProcessor::pick_point` — an
active OSNAP point overrides them. Grid snap rounds free picks to the grid.
Function keys toggle modes app-wide: **F3** OSNAP, **F7** grid, **F8** ortho,
**F9** snap, **F10** polar; the status bar reflects each. ERASE accepts cursor
picks (`ErasePickCommand` → `pick_nearest` via the shared index), closing the
Phase-4 selection gap, in addition to `Last`/`All`.

## Ribbon frame, cursor & autocomplete (Phase 6)

Presentation-only phase; no command logic changed.

* **Ribbon UI** — `RibbonBar` (Quick Access Toolbar + tab bar + stacked pages)
  and `RibbonPanel` (labelled icon+label button groups) replace the flat
  toolbar. The frame is set as the `QMainWindow` menu widget; the central widget
  stacks file tabs / viewport / layout tabs. Every ribbon button calls an
  **existing** command (`processor_->submit_line("L")`, `undo()/redo()`,
  `viewport_->zoom_extents()`); unbacked slots (Move/Copy/Trim/Layers/Dim…) are
  **disabled placeholders**. Status-bar mode toggles are `QToolButton`s bound to
  the F3/F7/F8/F9/F10 actions. All styling lives in one place
  (`theme.cpp::dark_theme_qss()`) plus object names — swappable, nothing
  hardcoded per widget. (Phase A below makes the command buttons registry-driven.)
* **AutoCAD cursor** — the render-side crosshair gained a center **pick-box**;
  it remains pure render-side (cursor atomics → drawn every frame), zero-lag, and
  folds into the existing single crosshair draw call (viewport stays at 4 scene /
  6 with aids).
* **Command autocomplete** — `CommandRegistry::suggest(prefix)` matches the
  prefix against aliases **and** full names, driven entirely by the existing
  alias table (single source of command truth). The command-line widget shows a
  dropdown while idle; Down/Tab and Up navigate, Esc closes, Enter runs the typed
  command if complete else accepts the highlighted suggestion, and empty-line
  Enter still repeats the last command. A `set_enter_picks_first()` flag switches
  to "Enter always picks the highlighted suggestion".

## Registry-driven ribbon icons + tooltips (Ribbon Phase A)

The command **registry is the single source of presentation truth**: each
`register_command(aliases, factory, icon, description)` row now carries the SVG icon
path and a one-line description (both **required** parameters, so a new command can't be
added without them). `CommandRegistry::find(alias)` returns a `CommandInfo
{name, primary_alias, icon, description}`; `CommandSuggestion` also carries the
description (a richer autocomplete dropdown is then a near-free follow-up).

* **The ribbon reads the registry** — `add_cmd(panel, label, alias)` pulls the icon +
  tooltip from `processor_->registry().find(alias)`; there is no ribbon-side per-command
  table. A command button's tooltip is rich text: `<b>NAME (ALIAS)</b><br>description`
  (the alias shown only when it differs from the name). Adding a future command's icon +
  description in its registration row is sufficient — no separate "wire it into the
  ribbon" step. Non-command buttons (file ops, zoom, layer manager, …) set their icon by
  asset path directly, since they aren't registry commands.
* **Icons are Musa-authored SVGs** under `assets/ribbon/*.svg` (49 files), 24×24,
  `stroke="currentColor"`, ≤2 colours — hand-drawn as part of this LGPL codebase, **not**
  a third-party icon set (no new dependency / license inventory entry). They are compiled
  into the binary via `assets/ribbon/ribbon.qrc` (AUTORCC, like `branding.qrc`).
* **Loading** — `command_icons.cpp::ribbon_icon("assets/ribbon/x.svg")` resolves the asset
  to the Qt resource `:/ribbon/x.svg`, bakes `currentColor`→the theme grey, and rasterises
  it crisply via the **qsvg image plugin** (the same plugin the branding logo uses — no
  `Qt6::Svg` link needed). Results are cached by path; an empty/missing/invalid asset falls
  back to the generic placeholder square (`make_icon`), so the ribbon degrades gracefully.
* **Status bar** — `make_mode_action` gained a description, giving the OSNAP/GRID/ORTHO/
  SNAP/POLAR/DYN toggles the same `<b>NAME (Fn)</b><br>description` tooltips.

### Progressive panel collapse + contextual tabs (Ribbon Phase B)

* **Three panel states.** `RibbonPanel` re-renders itself in `PanelState::{Full, Compact,
  Collapsed}`. FULL = all buttons large (icon + label); COMPACT = `RibbonTier::Primary`
  buttons stay large, `Secondary` shrink to icon-only; COLLAPSED = the whole panel folds to
  one fly-out `QToolButton` (its representative icon + title) whose click pops out the full
  panel in a child `QFrame` inline below the tab strip (captureable + click-outside dismiss
  via an event filter; dropdowns inside still work). Each button registers a **tier**, each
  panel a **priority** (higher = collapses last) + a representative icon -- all passed at
  registration (`add_panel(page, title, priority)`, `add_button(..., tier)`), so the layout
  reads from data, no collapse-side table. Priorities: Draw 100, Modify 95, Annotation 60,
  Layers 55, File 50, Properties 40, other-tab panels 20.
* **One layout algorithm (synchronous, force-settled).** `RibbonBar::relayout_page` (run from
  `resizeEvent`/show/tab-change via a coalesced `schedule_relayout`, which defers ONE tick only so
  the resized scroll-viewport width is valid) does the whole collapse in a single synchronous pass:
  reset every panel to FULL, then `while (page->sizeHint().width() > viewport)` collapse exactly one
  panel — lowest priority first, FULL→COMPACT across panels then COMPACT→COLLAPSED — re-measuring
  after each step. The horizontal **scroll bar** is the final fallback when even all-collapsed
  overflows. Only the active page lays out (others on show). **Why it must force-settle, and why an
  earlier *deferred* fitter was wrong:** measuring `sizeHint()` right after `set_state()`
  under-reports the row by ~170px — a panel returning from COLLAPSED reparents its content back
  (RibbonPopout→RibbonPanel), and the restyled `QToolButton`s only recompute their sizeHint after a
  *deferred* `QEvent::Polish`. A fitter that measured on a `singleShot(0)` tick **raced** that
  Polish: when the timer won, it read the stale width, under-collapsed, and the row overflowed into
  a scroll bar that stole ~14px of height and **clipped every panel title on the first maximize**
  (a tab switch papered over it because already-FULL panels don't reparent). The race was
  machine-dependent (offscreen Xvfb happened to win the other way, hiding it). The fix:
  `RibbonPanel::force_settle()` (called at the end of every `set_state`) makes the panel's sizeHint
  accurate **in-line** — `ensurePolished()` (synchronous QSS re-resolution) + per-`QToolButton`
  `updateGeometry()` (flushes the cached sizeHint the posted StyleChange invalidated — `ensurePolished`
  alone does NOT) + `content_` layout `activate()`. So the synchronous loop always measures the true
  width (empirically 1939, never the stale ~1910/1794), independent of event-loop timing. Hardening:
  an `in_fit_` re-entrancy latch (an `activate()`-induced resize can't re-enter), an RAII guard that
  re-enables `setUpdatesEnabled` on every exit path, and a one-shot ground-truth re-check
  (`did_recheck_`, gated by `fit_generation_`, skipped when all-collapsed) as a defensive net that in
  practice never fires. `RibbonBar::active_page_fits()` is the test hook (harness kinds 16/17 assert
  it). The tab bar's base line is off (`setDrawBase(false)`). The app launches **maximized**
  (`main.cpp` `showMaximized()`, gated off for the capture harness which sets its own geometry).
* **Styling (theme.cpp).** `QToolTip` is themed dark (the default pale-yellow tip was
  unreadable). Split/dropdown buttons use a small chevron (`:/ribbon/chevron-down.svg`) at
  the bottom-right via the `::menu-button`/`::menu-arrow`/`::menu-indicator` subcontrols,
  instead of Qt's default full-height sunken menu-button strip.
* **Contextual tabs.** `add_contextual_tab(title, accent, predicate)` builds a page that
  lives permanently in the stack but whose **tab** is shown/hidden by `update_contextual`,
  called from the 100ms selection poll (`sync_ribbon_context`, reusing the PR's signal --
  no new selection callback). Predicates take a plain-int `RibbonSel{count, mixed,
  kind_plus1, family_plus1}` (built in MainWindow with `core::family_of`, keeping RibbonBar
  core-agnostic). The three editors: **Hatch** (family Hatch), **Text** (family Text, so
  TEXT+MTEXT both match), **Block** (a single Insert). Multi-selection is **all-or-none**
  via `family_plus1`/single-kind (`SelectionSummary` gained `family_plus1`); a mixed
  selection shows none. A newly matched tab **auto-activates**; when the active contextual
  tab disappears the ribbon returns to the last fixed tab (Home). **Tab↔page indices are
  decoupled** through `tabData` (the page index), so contextual tabs add/remove without
  desyncing the stable pages. Accent: a `QTabBar` subclass paints a coloured top stripe per
  contextual tab (visible even when selected, where QSS `:selected` would override
  `setTabTextColor`). Order: contextual tabs always append after the fixed tabs; multiple
  may show at once (one per matched predicate), though the predicates here are mutually
  exclusive so it's one at a time in practice.
* **Dropdown grouping + overflow.** `RibbonPanel::add_dropdown(icon, label, menu, split)` adds
  an AutoCAD-style button with a dropdown of related commands -- `split` true = a split
  button (main click runs the primary, the arrow opens the menu), false = the whole button
  opens it. The Home tab groups families this way: Annotation collapses to **Text ▼ /
  Dimension ▼ / Leader ▼**, Modify to **Trim ▼ (Trim/Extend)** and **Fillet ▼
  (Fillet/Chamfer/Join)**, File to **Import ▼ / Export ▼**. (NB: a dropdown's QMenu must NOT
  be `setParent()`-ed into the button -- that strips its `Qt::Popup` flag and the menu
  renders inline; `setMenu()` alone links it while it stays the MainWindow's child.) Each
  ribbon page is wrapped in a **horizontal `QScrollArea`** and panels carry a `Minimum` size
  policy, so when the window is too narrow the row scrolls instead of compressing buttons
  until their labels overlap. (Full AutoCAD-style panel *collapse* -- a panel becoming a
  single fly-out button when space is tight -- is a further Phase B step.)

The threading invariants are unchanged: the UI thread still never touches the
store, commands still cross only via the MPSC queue, and the crosshair/pick-box
need no geometry round-trip. See [COMMANDS.md](COMMANDS.md) for the living
command roadmap.

## Live preview, selection & modify (Phase 7)

### Transient overlays are render-side, never real geometry

The command preview (rubber-band) and the selection rubber-band rectangle are
**transient render-side visuals**, composed on the UI thread from the active
command's `PreviewSpec` + the live (constrained) cursor and handed to the
renderer in a mutex-shared `RenderOverlay` -- the same UI->render pattern as the
camera and crosshair. They never create entities, never touch the store, op-log,
undo stack, or spatial index, and involve no synchronous per-move geometry
query. A draw commits exactly once, on the click, via the existing command
message. (Proven by `tests/test_preview_no_mutation.cpp`: anchoring + dragging a
preview emits zero geometry commands; the commit emits exactly one.) Move/mirror
"ghosts" are drawn by the renderer applying the overlay's transform to the
snapshot's `selected_line_vertices` -- still render-side, still zero round-trip.
Previews honor ortho/polar/grid-snap by resolving the cursor through the same
`CommandProcessor::resolve_pick` the commit uses, so preview == commit.

### Selection set (geometry-side, one index)

The selection set lives on the geometry thread (it is about entities) and is
published in the snapshot as `selection` (the queryable handle set -- the clean
API for the command layer and future scripting) plus `selected_line_vertices`
(for the highlight and ghosts). Single-pick, window (fully enclosed) and crossing
(touched) tests all run against the **existing Phase-5 spatial index**; the
rectangle visual is render-side. Drag direction (left->right vs right->left)
chooses window vs crossing, per AutoCAD. The selection is pruned of stale handles
on every rebuild.

To keep cursor/selection publishes cheap, the snapshot now carries a separate
`geometry_version` that bumps only when scene geometry changes; the renderer
keys its scene re-upload on that, so cursor moves, snapping and selection changes
cost no scene re-upload (constraint B still holds: 0 bytes over 300 camera
frames, ~470 FPS on the 1M scene).

### Modify operations

MOVE/COPY/MIRROR consume the selection; OFFSET/TRIM are pick-based. The op-log
undo model was generalized so a group's items each carry a create/erase flag --
MOVE/MIRROR/TRIM (erase originals + create results) are therefore a single
undoable group. OFFSET added `IGeometryKernel::offset` (line/circle/arc/polyline)
-- the first and only caller, per the minimal-interface rule. TRIM implements the
solid subset: trimming a **line** to its nearest intersections (cutting edges =
all nearby entities, via the index); trimming arcs/circles is marked Planned in
COMMANDS.md. Modify-panel buttons that need a selection (Move/Copy/Mirror) are
disabled until one exists, driven by the published selection count.

Draw-call count stays bounded and constant (not per-primitive): 4 for the scene,
6 with crosshair+pick-box+snap-marker, up to ~10 with selection highlight +
preview + ghost + selection rectangle all active.

## Empty startup, Delete, and full OSNAP (Phase 8)

* **Empty Model space on launch** — `MainWindow` only seeds the demo/benchmark
  scene when `MUSACAD_DEMO` is set; normal launch is empty. The perf harnesses
  (`bench_insert`, `render_offscreen`) build their own scenes, so they are
  unaffected.
* **Delete erases the selection** — `Delete`/`Backspace` in the viewport (when a
  selection exists and no command is active) submit `EraseSelectionCommand`,
  which captures + removes each selected entity as one undo group (reusing the
  op-log erase path) and clears the selection. `Esc` still clears the selection
  without deleting. Context-sensitive: handled in the viewport's key handler, so
  `Delete` still edits text in the command line.
* **Full OSNAP set** — the Phase-5 engine gained Quadrant, Node, Perpendicular,
  Tangent and a Musa-extension Centroid (closed-polyline centre). Everything
  still runs geometry-side through `compute_snap` against the spatial index and
  rides the existing snapshot (`has_snap`/`snap_point`/`snap_type`); markers are
  render-side, one bounded draw call. Each type is individually toggleable via a
  running-osnap mask (`SetCursorCommand::snap_mask`, set from the OSNAP button's
  dropdown). The deferred snaps (Perpendicular, Tangent) need the active
  command's previous point, passed as `SetCursorCommand::from` from
  `CommandProcessor::active_from()`. Precedence (lower wins) is the `SnapType`
  enum value: Endpoint < Midpoint < Center < Node < Quadrant < Intersection <
  Perpendicular < Tangent < Centroid < Nearest. To keep cursor-move publishes
  cheap the renderer still gates scene upload on `geometry_version`, so snapping
  costs no scene re-upload. Measured snap cost: ~31 µs/query with all types on a
  ~4900-entity scene (one query per frame, on the geometry thread — cursor and
  crosshair stay render-side and zero-lag).

## Delete fix, snap-marker visibility & hover (Phase 9)

* **Delete-key fix (focus/routing).** Two problems. (1) The Phase-8 `Delete`
  handler lived in the viewport `QWindow::keyPressEvent`, which the embedded
  native GL child window does not reliably receive. (2) More importantly, the
  command-line `QLineEdit` holds keyboard focus by default, so a naive
  "skip Delete when a text field is focused" guard blocked the binding *always*.
  Fixed with an **application-wide event filter** (`qApp->installEventFilter` in
  `MainWindow`) catching `Delete`/`Backspace` regardless of which window has
  focus, guarded by `CommandLineWidget::is_typing()` — which is true only when the
  field is focused **and non-empty** (i.e. actively being edited). An empty, idle
  command line therefore does not block Delete-erases-selection, while a command
  being typed keeps Delete for text editing. Verified on the **real window**
  (Wayland) via `MUSACAD_SELFTEST` (posts real Delete `QKeyEvent`s): erase with
  the command line empty+focused PASS, and the typing guard PASS.
* **Snap markers** are centralized in `render_theme.hpp` (one place for all
  render colors/sizes; three documented entity states — normal `kScene`,
  hover `kHover`, selected `kSelected`). Markers are now bright lime, larger, and
  bold (overdrawn at small offsets, since core-GL line width is unreliable),
  drawn on top, keeping the distinct per-type glyphs. Still one bounded draw call.
* **Rollover (hover) highlight.** On each coalesced cursor update the geometry
  thread computes the entity under the pick-box with the **same** `pick_nearest`
  query a click uses, and publishes it in the snapshot (`hover` handle +
  `hover_line_vertices`); the renderer highlights it in the hover color beneath
  the selection highlight. It's visual only — the selection set changes only on
  click — and a selected entity is not also hover-highlighted. Same
  snapshot-based, render-side, no-extra-handoff pattern as snap candidates. The
  pick aperture is always sent in `SetCursorCommand` (decoupled from the OSNAP
  on/off flag) so hover works with OSNAP off. Cost: hover pick ~0.7 µs/query;
  combined cursor-frame geometry-side cost (snap + hover) ~34 µs on a ~4900-entity
  scene — cursor/crosshair stay render-side and zero-lag.

## Completing the Modify suite (Phase 10)

Rotate/Scale/Array/Extend/Trim/Fillet/Chamfer, all on the existing machinery
(selection set, render-side ghost preview, op-log undo groups, spatial index).

* **Transforms.** `rotate_cmd`/`scale_cmd` join `translate_cmd`/`mirror_cmd` as
  pure Command transforms; `apply_rotate`/`apply_scale` are erase-original +
  create-result groups (like move); `apply_array_rect`/`apply_array_polar` keep
  the originals and add copies (like copy) — all one undo group. ROTATE and SCALE
  show a render-side ghost (overlay `ghost_mode` 3=rotate/4=scale with
  `ghost_param`). ARRAY is command-line driven (rows×cols×spacing, or
  centre+count+fill+rotate); the interactive dialog/preview is deferred.
* **Shared intersection primitives (built once in `NativeKernel2D`).**
  `line_line_intersection` (infinite, exact) and `line_circle_intersection`
  (infinite line ∩ circle) are the basis for every corner op; `intersect()` now
  takes an exact analytic path for line∩line, line∩circle, line∩arc (sweep-
  filtered) and falls back to tessellation only for arc∩arc and polyline/spline
  pairs. This also made OSNAP intersection ~4x cheaper. **Robust/exact:** line∩
  line, line∩circle, line∩arc. **Deferred (tessellation-approx):** arc∩arc,
  polyline/spline pairs.
* **Corner ops.** EXTEND lengthens a *line* to the nearest forward boundary
  (line/circle/arc), scanning live entities. TRIM cuts a *line* at its
  intersections with any nearby edge (now exact). FILLET joins two *lines* with a
  tangent arc (radius 0 = clean corner via trim/extend to the infinite-line
  corner); the kept side of each line is the picked side. CHAMFER bevels two
  *lines* by two distances. All reuse `line_line_intersection` + a shared
  `kept_endpoint` helper — no duplicated intersection math. **Deferred:** trimming/
  extending/filleting *arc entities* (the cutter/boundary may be a curve, but the
  entity being modified must currently be a line).

## Honest command feedback + polyline-corner Modify (Phase 10.1)

Follow-up after testing Modify on a **rectangle** (which is a single closed
polyline, not four lines):

* **The false-success bug.** Pick-based ops (Fillet/Chamfer/Extend/Trim) only
  handled `Line` entities, so they silently no-op'd on a polyline while the
  command still echoed "Filleted." The command line (UI thread) cannot see the
  engine outcome, so it was *asserting* success.
* **Honest status channel.** `RenderSnapshot` now carries a `status` string +
  `status_version`; the geometry thread's `report()` records what each op actually
  did (success or a specific reason it couldn't), published with the snapshot. The
  viewport copies it; `MainWindow`'s timer echoes each new message once to the
  command line. Pick-based commands no longer echo a guessed result — the engine
  tells the truth.
* **Polyline-corner Fillet/Chamfer.** When both picks land on the same polyline,
  the engine finds the two segments (`nearest_pl_segment`), their shared vertex
  (`shared_vertex`), and rewrites the vertex list: Chamfer replaces the corner
  with two bevel points; Fillet replaces it with a tangent arc approximated by
  vertices (our polylines are pure point lists — no arc bulges). Two-line Fillet/
  Chamfer are unchanged. **Deferred:** trimming/extending a polyline/arc *entity*
  (reported honestly), and true arc-bulge polyline fillets.
* **CHAMFER Angle method.** `CHAMFER` now offers `[Angle]` (chamfer length +
  angle, default 45°) alongside the two-distance method.
* **Verified end-to-end.** A real-window `MUSACAD_SELFTEST` (default runtime
  state — command line focused/empty) drives ARRAY and CHAMFER through the command
  line and confirms the rendered geometry changes (vertex count) **and** the
  engine's result reaches the command-line scrollback, including the honest
  "pick two adjacent edges" failure. Scale/Array were never broken (engine + GUI
  confirmed); the original report conflated them with the polyline no-op.

## Command input dialogs (Phase 11, slice 1)

AutoCAD-style parametric input, without disturbing the command pipeline.

* **Reusable `ParameterDialog`.** A `QDialog` built from a declarative
  `DialogSpec` (Number/Integer/Choice/Bool fields). A field's `group` gates its
  visibility against a controller Choice, so one dialog presents alternative
  parameter sets (e.g. ARRAY's Rectangular vs Polar). It emits `valuesChanged()`
  for a future live preview, and has programmatic setters used by the self-test.
* **No new pipeline.** A dialog only *collects parameters*; the caller submits the
  same `Command` the command-line flow would. `CommandProcessor::begin_group()`
  opens a one-shot undo group for a button/dialog-initiated command. The ARRAY
  ribbon button opens the dialog; typing `AR` still runs the command-line Q&A.
  Both end at the existing `ArrayRect`/`ArrayPolarCommand`.
* **Verified.** `MUSACAD_SELFTEST` builds the ARRAY dialog (default runtime
  state), sets fields, accepts, and confirms both Rectangular (6 instances) and
  Polar (4 instances) arrays reach the engine and grow the rendered geometry.
* **Deferred to slice 2:** live ghost preview as fields change, and dialogs for
  Rotate/Scale (value + a "pick base point" affordance).

## Persistence: native save/open + DXF (Phase 11)

Drawings become durable, via a module under `core/io/` (own `musacad::core::io`
namespace; DXF isolated in `dxf.cpp`). The serializable IR is `Document` -- a
pool-free, generation-free struct holding every entity family -- with
`document_from_store()` / `populate_store()` bridging to the SoA store.

* **Native `.musa` format.** Chosen form: a **versioned, line-oriented text**
  format (one record per line; a `MUSACAD <version>` header; `END` terminator).
  Doubles are written with `std::to_chars` and read with `std::from_chars` --
  shortest *exact* round-trip, locale-independent -- so **save → load reproduces
  the document value-for-value**. Proven by a test that builds a scene with every
  entity family, round-trips it, and asserts `Document` equality (and store →
  doc → file → doc → store equality).
* **Load = one geometry-thread op, fail-safe.** `Open`/`New` are messages handled
  on the geometry thread. A load first *parses fully into a Document*; only on
  success does the engine clear the store + grid, repopulate, rebuild the spatial
  index, and reset undo/redo/selection. **On any parse failure the store is left
  untouched** -- no partial loads. Undo after a load cannot resurrect pre-load
  geometry (history is reset).
* **DXF (AC1015 / R2000).** Export writes HEADER + TABLES (layer "0") + ENTITIES
  so the file is genuinely loadable; entities: LINE, LWPOLYLINE, CIRCLE, ARC,
  POINT (arc angles converted radians↔degrees). Import is a tolerant group-code
  parser: supported entities load; unsupported ones are **counted and named in
  the result** ("skipped 2 unsupported (HATCH, MTEXT)"), never fatal. Malformed
  input (dangling code, non-numeric code, empty) fails with the store unchanged.
  **Externally verified:** Musa's DXF export loads + renders in **LibreCAD**
  (`librecad dxf2pdf`). Deferred: SPLINE and legacy POLYLINE import.
* **Dirty tracking + feedback.** The engine sets `dirty_` on any mutating command
  and clears it on save/open/new; `dirty` + `document_version` ride the snapshot
  next to the Phase-10 status string. The UI title shows `name* — Musa CAD`, and
  New/Open prompt before discarding unsaved work. The UI shows the file dialog and
  issues a message but **never touches the store**.

## Consistent dark theme (dialogs included)

The frame was themed only by a QSS stylesheet, which native-style surfaces
(QFileDialog, QMessageBox, menus, window chrome) ignore -- so they rendered
light. `apply_dark_theme()` (the single startup entry point) now sets the
**Fusion** style, a **dark QPalette**, the **dark color-scheme hint**
(`QStyleHints::setColorScheme`, Qt 6.5+; compiled out on 6.4), and the QSS. The
palette is what reaches surfaces QSS can't, so dialogs match. File dialogs use
`QFileDialog::DontUseNativeDialog` so they render with our palette regardless of
the OS theme. (Server-side window-manager decorations are outside the app's
control on Qt 6.4; the color-scheme hint requests dark there on 6.5+.) Verified
by `MUSACAD_SELFTEST`: a freshly-built file picker and message box both inherit
the dark palette.

## Layers & properties (Phase 12)

A cross-cutting addition: the layer table and the ByLayer/override property model.

* **The model (`core/properties.hpp`).** `EntityProps` carries a layer index plus
  per-property ByLayer flags + override values; `Layer` holds name/colour/linetype/
  lineweight + on/frozen/locked. `resolve(EntityProps, Layer)` is the conceptual
  core: an explicit override wins, else the value is inherited from the layer.
  `EntityProps` is deliberately **8 bytes** (packed flags byte + `uint8`
  hundredths-mm lineweight, no `double`) so it's cheap on every entity -- the
  initial 24-byte version regressed inserts and was slimmed.
* **Store.** Each Data struct gains an `EntityProps` column; the store owns the
  layer table (`layer 0` permanent at index 0) and the current layer. Layer CRUD
  is geometry-thread. **Delete rule (AutoCAD):** layer 0, the current layer, and
  non-empty layers can't be deleted; layer 0 can't be renamed; on removal, higher
  entity layer-refs shift down.
* **Creation & undo.** Fresh draws stamp the current layer (the `Add*` command's
  optional `EntityProps` is empty); `capture_entity` records exact props, so undo/
  move/copy/array/mirror/fillet/chamfer preserve layer + overrides. Property edits
  on a selection are erase+recreate groups, so they're undoable.
* **Snapshot / render (geometry-side resolution).** `build_render_snapshot`
  resolves each entity's effective colour, **skips off/frozen layers** entirely,
  and groups visible geometry into per-colour `ColorBatch`es. The renderer draws
  one small sub-range per colour (no shader change; draw calls scale with distinct
  colours, not entities). The snapshot also carries the layer table + current
  layer for the UI. **Pick/select** (`selectable()`) skips off/frozen/**locked**
  entities, so locked layers are drawn but inert across hover, click, window-
  select, erase, and every Modify pick. Linetype/lineweight are carried + round-
  tripped; visual stipple/weight rendering is deferred.
* **UI.** A dark, modeless Layer Manager (`LayerDialog`) -- table of name/on/
  frozen/locked/colour/linetype/lineweight with New/Delete/Set-Current/Assign --
  plus a ribbon current-layer combo and a Set-Colour override button. It reads the
  layer table from the published snapshot and issues commands; it never touches
  the store.
* **Persistence.** Native format **v2** serialises the layer table + per-entity
  props (round-trips losslessly; **v1 files load onto layer 0**, fully ByLayer).
  DXF writes a real **LAYER table** (TABLES section) and per-entity layer (code 8)
  + ByLayer colour (62 = 256) or true-colour override (420); import **reads the
  LAYER table** and assigns entities to real layers -- the Phase-11 faked default
  is gone. Verified Musa↔DXF (layers + effective colour) and in LibreCAD.

## Text & dimensions (Phase 13)

* **Vector text (`core/text/stroke_font`).** A single-stroke font (not an atlas/
  SDF): text is geometry, so it stays crisp at every zoom and batches with the
  existing line pipeline -- no texture backend. Covers ASCII 0x20-0x7E plus UTF-8
  `° ± ⌀`; lowercase renders as small capitals (a documented CAD-font
  simplification). `append_text_segments(text, origin, height, rotation, justify)`
  emits world-space segments; `text_width` drives justification + pick bounds.
* **TEXT entity.** `EntityKind::Text`; `TextData` (pos/height/rotation/justify +
  an (offset,len) into a shared char pool -- no fat inline buffer). Layer-aware,
  selectable (bbox), movable/rotatable/scalable (the transform helpers handle it),
  erasable. The snapshot tessellates each text to stroke segments in its resolved
  colour.
* **Dimensions.** `EntityKind::Dimension`; `DimData` stores **definition points**
  (a, b) + a placement point + a dimstyle index -- the measured value is
  **computed**, never baked (`dim_measure`), so editing the def points updates the
  dimension on the next snapshot. `compute_dim_geometry` (shared by the snapshot,
  the kernel pick path, and bounds) builds extension lines + the dimension line +
  arrowheads + the measured label under the style. **Solid: DIMLINEAR + DIMALIGNED.**
  Radius/Diameter/Angular are modelled (measure works) with geometry staged.
* **DIMSTYLE.** A style table parallel to the layer table ("Standard" at index 0):
  text height, arrow type (filled-triangle fan / tick) + size, extension offset/
  extension, decimal precision, text placement. Changing a style re-renders the
  dimensions that use it (snapshot recompute). Editable via a dark dialog.
* **Arrowheads** are real oriented geometry batched with everything else (a fan of
  lines fakes a filled triangle for the line renderer); draw calls stay bounded
  (per-colour batches -- 4 in the offscreen scene).
* **Associativity (honest level):** a dimension recomputes its value/label/geometry
  from its stored def points every snapshot build. Editing the def points (or the
  style) updates it; moving the *referenced entity* does **not** auto-update the
  dimension (def points are independent copies). True geometric association is
  deferred.
* **Struct sizes:** `TextData` 56 B, `DimData` 72 B -- their own arenas, so the hot
  `LineData` stays 40 B (insert baseline unchanged, ~30-50 ns/line).
* **Persistence:** native **v3** round-trips text + dims + dimstyles losslessly
  (v1/v2 still load -- no annotations); DXF writes/reads TEXT + linear DIMENSION +
  the DIMSTYLE table (LibreCAD-verified).

## Lineweight rendering & solid arrowheads (Phase 14)

* **Thick lines via screen-space expansion (not `glLineWidth`).** A second line
  pipeline (`shaders/thickline.*`, `Topology::TriangleStrip`) draws each segment as
  an instanced 4-vertex quad expanded perpendicular to the segment by a pixel
  half-width in the vertex shader. Core-GL `glLineWidth` is unreliable/capped, so it
  is not used for scene geometry. Caps are square (each end extended by the
  half-width) so polyline joins overlap with no gaps -- a cheap stand-in for true
  miter/round joins (deferred).
* **mm -> pixels.** Lineweight (the Phase-12 per-entity uint8 hundredths-mm,
  resolved ByLayer/override) maps to a *fixed screen* width `px = max(1.5, mm*6)` --
  zoom-independent, matching AutoCAD's default lineweight display. "Display to
  scale" (world-proportional) is deferred. Offscreen-measured: LWDISPLAY-off ~1 px,
  default 0.25 mm ~2 px, 1.20 mm ~8 px.
* **LWDISPLAY.** A drawing-wide flag (`SetLineweightDisplayCommand`, default **on**)
  published in the snapshot and read per-frame at draw; off -> a thin 1 px default.
  A checkable **LWT** ribbon button toggles it.
* **Batching stays bounded.** Lines batch by **(colour, lineweight)**; fills by
  colour. A 1,000,000-line scene still draws in <= 6 calls at ~94 FPS offscreen
  (4-vertex expansion vs the old 2-vertex line). Insert baseline unaffected
  (~30 ns/line) -- rendering is downstream of the store.
* **Solid arrowheads (`Topology::Triangles`, `shaders/fill.*`).** A fill channel
  carries filled triangles (3 Vec2 each) batched by colour. `append_arrowhead`
  emits **filled / dot** as triangle fans and **open / tick** as line segments,
  oriented and sized to the style. Shared by dimensions and leaders.
* **DIMSTYLE colour expansion.** The style carries per-element `ElementColor`
  (ByLayer-or-explicit) for dim line / extension line / text / arrowhead, plus a dim
  lineweight. `compute_dim_geometry` returns each element list with its resolved
  colour; the snapshot routes them into the right batches. A style change re-renders
  every dimension that uses it; the dark dimstyle dialog edits these.
* **Remaining dimension types + leader.** On the shared model (def points ->
  computed geometry): **DIMRADIUS** (centre->edge, R-prefix), **DIMDIAMETER**
  (through-centre, diameter prefix, two arrows), **DIMANGULAR** (vertex + two ray
  points -> arc + degree). **Leader** is a new `EntityKind::Leader` (`LeaderData`:
  tip + knee + text in the shared char pool, own arena) sharing the arrow +
  stroke-font machinery. All are layer-aware, selectable, undoable, round-trip the
  native format; DXF writes each DIMENSION subtype (group code 70) + a LEADER (the
  leader label re-imports as a separate TEXT -- stated honestly).
* **Struct sizes:** `LeaderData` 64 B and `DimStyle` 96 B in their own arena/table;
  the hot `LineData` stays **40 B** and insert holds at ~30 ns/line.
* See `docs/AUTOCAD_CONFIG.md` for the full configurable-options roadmap.

## HATCH: SOLID fills + boundary tracing (Part A)

* **Entity model.** `EntityKind::Hatch`; `HatchData` keeps only the boundary as
  **closed loops in world coords** (loop 0 = outer, the rest islands/holes) in a cold
  arena (`hatch_vtx_pool_` + per-loop lengths) plus pattern name/scale/angle/origin and
  the universal `EntityProps`. Hot structs stay unfattened (`LineData` still 40 B);
  `family_of(Hatch) = HatchFamily`. Like every other entity it round-trips the native
  format (v-bump) and DXF (codes 2/70/91/92/93/72/41/52/10-20), is MATCHPROP-matchable,
  and is editable through the Phase-22 PR registry (Pattern/Scale/Angle/Origin).
* **SOLID fill is derived, not baked.** `hatch::triangulate_filled` (an exact
  **trapezoidal decomposition** with the even-odd rule, so holes drop out with no
  ear-clipping or stair-stepping) runs at snapshot time and feeds the Phase-14 fill
  channel — so a hatch **plots as PDF vectors** with no special case, and SOLID is a
  pattern *name*, not a separate entity. (Line patterns = Part B.)
* **Two boundary modes (engine-side; the UI never touches the store).**
  `HatchFromSelectionCommand` turns pre-selected closed polylines into loops (even-odd ⇒
  nested loops become holes automatically). `HatchPickPointCommand` traces the boundary
  from an internal point: `hatch::trace_boundary` gathers candidate segments from all
  curve-like entities (tessellated), builds a **planar arrangement** — `split_at_intersections`
  cuts every segment at crossings and T-junctions so a *partitioning* chord wires into the
  edges it touches — then ray-casts from the pick and walks the enclosing face (both
  handedness, validated by point-in-polygon, smallest enclosing wins). Closed entities
  fully inside that don't contain the pick become islands. No closed boundary ⇒
  "Valid hatch boundary not found." (AutoCAD's message).
* **Selection + reshape.** A picked hatch publishes a grip at every boundary vertex
  (`grips_of`/`edit_for_grip_drag`, flat index across loops) so it reads as selected and
  each joint is **draggable to reshape**. Its fill triangles are also surfaced in
  `selected_fill_vertices` so the renderer **tints the whole fill** in the selection
  colour (the fill never vanishes on select).
* **Scene-fill buffer invariant (renderer).** `fill_buffer_` holds **persistent scene
  fills only** (arrowheads + hatches), re-uploaded just when `geometry_version` changes.
  Transient overlay fills (grip squares, grip-drag preview, the selection tint) MUST use
  the per-frame `aux_buffer_` — never `fill_buffer_`. Clobbering the scene buffer with
  overlay data makes fills vanish on the next non-geometry frame (selection) and flicker
  during zoom; keeping the two separate is what fixes both.
* **Line patterns (Part B).** `hatch_pattern.{hpp,cpp}` add a `.PAT` parser (`parse_pat`)
  and a built-in stock library (`builtin_pattern` / `builtin_pattern_names`) authored from
  the public .PAT format — ANSI31–ANSI38 plus common geometric fills; **not** copied from
  Autodesk's acad.pat (load that with `parse_pat` for the vendor set). A pattern is a list
  of line families `{angle, origin, delta-x (per-row dash stagger), delta-y (perpendicular
  spacing), dashes}`. `generate_pattern_segments` walks each family's parallel lines over
  the region's bbox and, per line, finds boundary crossings (half-open signed-distance
  rule, even-odd so islands carve out), emitting **solid spans or dash sub-segments clipped
  to the inside intervals** — derived-not-baked, scaled by pattern_scale, rotated by
  pattern_angle, anchored at pattern_origin. In the snapshot the hatch loop branches:
  `SOLID` → fill triangles; any known pattern → clipped **lines** routed into the normal
  (colour, lineweight) line batches (so they batch with all geometry and **plot as
  vectors**); unknown name → nothing. SOLID stays the special name — one entity, one
  snapshot path, patterns are not a fork. A per-family line cap guards tiny-scale blowups.

## Object-aware dimensioning & AutoCAD-accurate lineweight (Phase 15)

* **AutoCAD-accurate lineweight (corrected).** The Phase-14 mapping
  (`px = max(1.5, mm·6)`, a magic constant) is replaced by a DPI-anchored one:
  `px = max(1.0, mm × device_px_per_mm)` with `device_px_per_mm = screen_DPI / 25.4`.
  (Phase 19 corrects a HiDPI bug here: `physicalDotsPerInch` is actually a *logical*
  density, so the effective px/mm must also be multiplied by the device-pixel-ratio —
  see "HiDPI / device-pixel-ratio correctness" below.) The renderer defaults to a
  96-DPI assumption (~3.78 px/mm) for offscreen/test use.
  "Default" (and any sub-pixel weight) floors to a **1px hairline** — exactly how
  AutoCAD shows the default lineweight. Still zoom-independent (no camera scale).
  Verified pixel widths @96 DPI: 0.25mm→1, 0.50mm→2, 0.70mm→~3, 1.00mm→4, 2.00mm→8.
  LWDISPLAY off → 1px everywhere. The AutoCAD ladder (0.00…2.11 mm) is the stored
  set; the mapping is continuous over it.
* **Object-aware dimensioning.** Dimensions are created by *selecting the object*,
  not by picking raw construction points. A single message,
  `AddObjectDimensionCommand{type, pick1, pick2, pick_radius, …}`, is resolved on
  the **geometry thread** by `apply_object_dimension`: it `pick_nearest`-resolves the
  entity (via the spatial index + `selectable()` gate, so off/frozen/locked layers
  are respected) and reads the entity's intrinsic geometry —
  - **Radius/Diameter** ← a Circle/Arc's own centre + radius (edge along `pick2`),
  - **Linear/Aligned** ← a Line's endpoints, or the nearest Polyline segment
    (`nearest_pl_segment`, shared with fillet/chamfer); `pick2` is the dim-line
    placement,
  - **Angular** ← two Lines/edges: vertex = their intersection, rays from each
    direction.
  One code path per type; the UI commands and the smart DIM only *choose the type*
  and submit — they never duplicate the dimension math. The UI never touches the
  store (it submits a pick, the geometry thread reads the geometry).
* **Storage = def points only (no entity reference).** The resolved entity is
  converted to def points **at creation** and stored in the existing `DimData`
  (still 72 B; `LineData` still 40 B — no shared struct grew, no native/DXF format
  bump). This keeps the Ph13 "value computed from def points" rule and makes the
  **dangling-reference problem impossible by construction**: deleting (or editing)
  the source entity leaves the dimension intact with its captured geometry — the
  same associativity level as before (def-point edits update the value; the source
  entity is not back-referenced), stated honestly.
* **Smart DIM (dispatch, not duplicate).** `DimCommand` previews the type as the
  cursor rolls over candidates and dispatches on pick. Hover flows render-side →
  UI: the geometry thread already publishes `snapshot.hover`; the viewport caches
  its `EntityKind`, the main-window timer pushes it to the processor
  (`set_hovered_kind`), which fires the active command's new `ICommand::hover()`
  hook. `DimCommand::hover` updates the prompt ("…→ Diameter"); `DimCommand::input`
  reads `CommandContext::hovered_kind()` at pick time to choose
  circle→diameter / arc→radius / line·poly→linear, then submits the shared
  `AddObjectDimensionCommand`. (Two-line angular stays on DIMANGULAR.)

## Zoom-adaptive tessellation, round joins & dimension preview (Phase 16)

The "thin/dashed arc" report was a **continuity** problem, not a weight problem:
curves were sampled at a fixed *world-space* tolerance, so zooming in stretched the
chords into visible facets, and the square-cap join left notches. Both are fixed
here; stored geometry stays parametric throughout.

* **Zoom-adaptive curve tessellation (Part A).** `arc_segments` already sampled to a
  chord tolerance; the tolerance is now derived from the **view scale**. The viewport
  emits `SetViewScaleCommand{world_per_px}` **only when the camera scale changes**
  (detected once per frame in the render loop; zoom/resize change it, pan does not).
  The geometry thread buckets it to **half-octaves** (`round(log2(world_per_px)*2)`)
  and, only on a **bucket change**, sets `geom_dirty_` and rebuilds the snapshot with
  `tolerance = 0.3px × world_per_px`. So: panning never re-tessellates; a meaningful
  zoom step does; tiny zoom jitter within a bucket is a no-op. It rides the existing
  command queue + snapshot — no new handoff, no synchronous per-frame geometry query;
  the renderer still only reads snapshots. **Stored curves remain parametric**
  (center/radius/def-points) — only the per-snapshot render payload is sampled; no
  segment list is ever baked into storage. Work is bounded by `kMaxArcSegments`
  (8192/curve); a screen-filling circle needs only ~128 segments at 0.3px error, so
  the cap is generous headroom. Measured: 5000 circles re-tessellate 7→158 segs/circle
  coarse→fine and render (790k segments) in ~6 ms/frame at 4 draw calls.
* **Round joins/caps (Part B).** The thick-line shader no longer relies on
  square-cap extension to fill joins. The vertex stage passes the segment endpoints +
  half-width (flat) and the fragment stage keeps only fragments within the half-width
  of the segment (a **capsule SDF**, `discard` outside). Every segment thus has round
  caps; consecutive segments of a tessellated curve share a vertex, so their round
  caps overlap and fill the join with no gap or notch on the outer bend — proper round
  joins with **no extra geometry and no extra draw call** (the quad already extends by
  the half-width). Cost is one segment-distance test per fragment plus a little cap
  overdraw. Draw calls stay ≤ 6.
* **Dimension placement preview (Part C).** During the placement step every dimension
  rubber-bands the **full** geometry (ext + dim lines, arrowheads, and the live
  measured label) at the cursor, committing on click — the Phase-7 render-side preview
  pattern (`PreviewKind::Dimension`), composed UI-side in `rebuild_overlay` via
  `core::compute_dim_geometry`, honoring ORTHO/POLAR/snap through `resolve_pick`.
  Two-point dims carry their def points in `PreviewSpec.points`; **object** dims
  resolve their def points **once at the object pick** via the non-mutating
  `ResolveDimObjectCommand` (which shares `resolve_dim_defs` with the create path — no
  duplicate logic) and the geometry thread publishes them in the snapshot
  (`pending_dim_*`, plus `dimstyles` for the style); the per-cursor preview is then
  pure UI with **zero geometry round-trip and zero store/op-log mutation during the
  drag** (radius/diameter recompute the edge from the cursor; angular's geometry is
  fixed by its two lines so its preview is shown for confirmation). The commit path is
  unchanged. No shared per-entity struct grew (`LineData` 40 B, `DimData` 72 B); the
  snapshot gained transient preview fields only. Native/DXF formats unaffected.

## Grip editing — direct manipulation (Phase 17)

The first non-command editing path: select an entity, drag a grip handle, release to
commit. It reuses the transient-preview-then-commit discipline (Phase 7) for *editing*.

* **One grip module (`core/grips`), no duplicated logic.** `grips_of(store, h)` returns
  an entity's grips (per-kind), and `edit_for_grip_drag(store, h, index, newpos)` returns
  the **edited entity as an `Add*` command** (capture → mutate the relevant parameter →
  parametric result, props preserved). The engine's `capture_entity` and `create_entity`
  were extracted here (`capture_entity`, `add_command_to_store`) so the create/undo path,
  the move path, and grip editing all share one definition.
* **Transient, render-side, commit-on-release.** A `GripDragCommand{Begin|Move|Commit|Cancel}`
  drives the lifecycle on the geometry thread. `Begin` arms `(handle, grip)`; `Move`
  records the resolved target; `Commit` applies the edit as **one undo group**
  (capture original → remove → create edited, exactly the move pattern); `Cancel`/Esc
  drops it. **No store mutation or op-log entry during the drag** — the preview is
  computed by running the edited command through `build_render_snapshot` on a **reused
  temporary store** and published as `grip_preview_segments`/`fills` in the snapshot, so
  the real store and undo log are untouched until release. This rides the existing
  command queue + snapshot (no second handoff, no synchronous per-move query); the
  renderer only reads snapshots.
* **Grips on the snapshot.** The engine publishes `grips` (pos + handle + index + kind)
  for the selected set (gated by `selectable()` — off/frozen/locked layers get none) and
  a `hot_grip` (the grabbed grip, else the nearest to the cursor within the aperture).
  The renderer draws grip squares in **screen space** (fixed pixel size; blue, hot=red),
  batched into at most two fill draws; the drag preview is at most two more — draw calls
  stay bounded. The UI caches `grips` for GUI-thread hit-testing; a press near a grip
  sends `Begin` (and sets the processor's last-point to the grip origin so ORTHO/POLAR
  resolve relative to it); moves send `Move` with the `resolve_pick`-resolved target
  (OSNAP/ORTHO/POLAR honored); release sends `Commit`.
* **Per-entity grips.** Line (2 endpoints + midpoint-move), Circle (centre + 4
  quadrant-radius), Arc (centre + 2 endpoints[angle] + mid[radius]), Polyline/Rectangle
  (per-vertex move), Text (insertion). Dimensions get the richest set. **Phase 19**
  expands a Linear/Aligned dimension to a **full 5-grip set** — both extension-line
  origins (def points), **both dim-line ends (feet)**, and the offset midpoint — so it
  is grabbable from many points, not one central handle. Dragging a def point
  **re-measures** (live value); dragging any of the three dim-line grips (the two feet
  or the midpoint) slides the whole dim line to the cursor (value unchanged), placing
  the dimension anywhere. This needed **no edit-path fork**: `edit_for_grip_drag` maps
  grip index 0→`a`, 1→`b`, and any index ≥2→`line_pt`, so the extra feet are just more
  entries in `grips_of` — all on the existing `DimData` (no struct change). Radius/
  Diameter keep centre-move + edge-re-measure + placement; an independent text-only
  reposition grip stays deferred (needs a stored text offset + format bump). Object-based
  dims store def points only (Ph13/Ph15), so a def-point drag re-measures from the dragged
  points — the source entity is never back-referenced, so there is **no dangling reference**.
* **Stays parametric.** Dragging a circle quadrant changes its `radius`, not a baked
  segment list; dimensions stay def-points. No per-entity struct grew (`LineData` 40 B,
  `DimData` 72 B); grips are computed on demand, not stored. A grip-edited drawing
  round-trips native + DXF unchanged (it produces ordinary entities).

## Polyline arc segments (bulges)

Filleting a rectangle/polyline corner used to **bake** the round as ~13 straight
vertices, so it had no radius and couldn't be dimensioned. Polylines now carry true
arc segments (AutoCAD LWPOLYLINE-style **bulges**), so a fillet is a real arc with a
recoverable radius.

* **Model.** Each vertex `i` has a bulge `b = tan(θ/4)` for the segment `i→i+1`
  (`0` = straight; sign = direction). `math.hpp::arc_from_bulge(p0,p1,b)` recovers the
  arc (centre, radius, start angle, signed sweep). Stored in a parallel `bulge_pool_`;
  `PolylineData` gains a `bulge_offset` (sentinel `kNoBulges` for the common all-straight
  case, so straight polylines store **zero** bulge data). `PolylineData` grew 20→**24 B**;
  no other struct changed and the insert baseline is unchanged (~37 ns/line).
* **One geometry definition.** The kernel's polyline `tessellate` walks segments and
  samples bulged ones to the **zoom-adaptive chord tolerance** (Phase 16), so render,
  selection, hover, and pick all show smooth arcs from a single place; `entity_bounds`
  samples arc segments so the AABB encloses the bow. Stored geometry stays parametric —
  bulges, never baked facets.
* **Fillet produces a bulge.** `fillet_pl` now replaces the corner with its two tangent
  points and records the arc as a bulge on the first (no facets). Chamfer stays straight.
* **Dimensioning the fillet (the fix).** `resolve_dim_defs` Radius/Diameter accepts a
  **polyline**: it finds the arc segment nearest the pick and reads its bulge's
  centre+radius — so DIMRADIUS/DIMDIAMETER work on a filleted corner.
* **Edits + persistence.** `AddPolylineCommand.bulges` flows through capture/create and
  the transforms (mirror negates bulges; translate/rotate/scale keep them); grips move
  vertices and keep bulges (the arc recomputes). Native format **v5** appends per-vertex
  bulges (v1–v4 load as straight); DXF reads/writes LWPOLYLINE **code 42**
  (LibreCAD-verified). `DocPolyline.bulges` carries them in the IR.

## DWG import/export via an external converter (Phase 27)

DWG support the FreeCAD way: Musa CAD invokes a **separate external converter
program** and reads the DXF it produces. It reuses the existing DXF importer/
exporter end to end.

* **LGPL process boundary (the rule).** Musa CAD is LGPL; the DWG converters
  (LibreDWG = GPL, ODA File Converter = proprietary) are **never** linked, bundled,
  vendored, or added to the build (no FetchContent/vcpkg/find_package for any of
  them — verified: nothing DWG-related is in any CMakeLists). The converter is the
  **user's** install, discovered at **runtime**, and invoked as a subprocess. The
  licensing boundary is a process boundary — no GPL code enters the Musa CAD build
  or shipped artifacts.
* **Discovery + invocation** (`ui/dwg_converter.{hpp,cpp}`, Qt-only, in the UI layer
  so core stays Qt-free). `DwgConverter::discover()` order: (1) the configured
  `QSettings io/dwg_converter_path` (kind inferred from its basename), (2) ODA File
  Converter on `PATH`, (3) LibreDWG `dwg2dxf` on `PATH`. `to_dxf`/`to_dwg` are
  synchronous (`QProcess` + start/run timeouts + stderr capture, `QTemporaryDir`
  workspace) and build the per-kind command line: Generic `<prog> <in> <out>`,
  LibreDWG `dwg2dxf -y -o <out> <in>`, ODA's directory-batch protocol. No converter
  found → `install_hint()` (what to install / how to point Musa CAD at it) — a clear
  message, never a crash or silent failure. A **"DWG Setup" dialog**
  (`configure_dwg_converter`) makes that actionable: it shows the detected converter,
  lets the user **Browse** to one (`from_program`) or **auto-detect on PATH**
  (`discover_on_path`), links to the ODA/LibreDWG downloads, and saves the
  `io/dwg_converter_path` setting. When Import/Export DWG finds no converter it offers
  a "Configure…" button straight into this dialog (no dead-end). Auto-download/install
  is deliberately NOT done — a GPL/proprietary converter can't be fetched+installed for
  the user (licensing/EULA/per-platform installers/security); the user installs it, the
  dialog points at it.
* **Off the UI thread, responsive.** The conversion is a separate OS process; the
  blocking call runs on a short worker thread behind a modal indeterminate progress
  dialog that pumps the event loop (`run_with_progress`). The store is never touched
  during conversion.
* **Import = convert then the EXISTING fail-safe DXF load.** DWG → temp DXF →
  `open_from(tempDxf, dxf=true)` → `OpenDocumentCommand` → the geometry thread's
  `load_document_replace` (one op; store unchanged on any converter/parse error).
  The **gap catalog** is the existing importer's itemised `IoResult` message
  ("Imported N…; skipped K unsupported (HATCH x12, IMAGE x4, …)") — echoed to the
  command line **and** written to `<source>.dwg.import.log`, so migration gaps are
  recorded, not silently dropped. The prioritised fidelity backlog lives in
  `docs/TODO.md`.
* **Export = the EXISTING DXF export then convert.** `SaveDocumentCommand{tempDxf}`
  (geometry thread) → wait for the temp DXF → `to_dwg(tempDxf, out.dwg, "ACAD2018")`.
  Honestly two-stage lossy: capped first by Musa→DXF fidelity, then by the converter
  (dimensions/layers/linetypes/text round-trip; exotic content does not). Default DWG
  version ACAD2018 (widely compatible). External-tool verification is the user's to
  run (no converter in the build env).
* **Wiring + tests.** Ribbon Import/Export DWG buttons + `DWGIN`/`DWGOUT` aliases
  (via the `ViewControl` callback pattern). No data-model change; no second DXF path.
  Tests drive the full pipeline with a **mock converter** (`<prog> <in> <out>` that
  copies bytes, with a fake-DWG-that-is-DXF fixture) since no real converter exists in
  CI — proving discovery, off-thread convert, fail-safe load, the gap catalog, and the
  export round-trip.

## DYN autocomplete + parametric command dialogs (Phase 26)

Two input surfaces over the existing pipeline — no new command/coordinate/
suggestion logic.

* **DYN autocomplete = the Ph6 source at the cursor.** `DynInput` gains a
  `QListWidget` dropdown driven by `primary_->textChanged` → the **same**
  `processor_->registry().suggest()` the bottom command line uses (one suggestion
  source). It shows only while idle (a command token, not a value), anchored just
  below the cursor field in global coords. The popup is a top-level **`NoFocus`**
  window so it never steals typing from `primary_` (the focus lesson); DYN's
  `eventFilter` drives it with the Ph6 keys (Down/Tab next, Up/Backtab prev, Esc
  closes the list only, Enter runs-if-complete-else-accepts). It hides when DYN
  hides.
* **Draw/transform commands stay interactive (the AutoCAD model).** A ribbon draw
  button **starts the command** (e.g. `submit_line("C")`) exactly like typing the
  alias — it prompts *"Specify center point"* and the user **picks on screen**; it
  never opens a fixed-position dialog that places at 0,0. The cursor-anchored
  value surface during the command is **DYN** (Ph25), which mirrors the live prompt
  and accepts the same input as the command line — that is the "floating box," not a
  separate modal. (An earlier attempt at upfront draw modals was removed: a modal
  that asks for position cannot be "identical to the command line"; DYN is.)
* **Command options work identically in both surfaces.** Option keywords are part
  of the command's state machine, so the command line *and* DYN get them for free.
  CIRCLE now offers *"Specify radius or [Diameter]"* — typing `D` switches the value
  step to a diameter (the prompt, which DYN mirrors, shows the option). RECTANGLE's
  length/width and the rubber-band come through DYN's live dimensional input (Ph25).
* **Multi-parameter dialogs stay the Ph11 ParameterDialog.** ARRAY (which has many
  parameters and matches AutoCAD's array dialog) keeps its collect-and-submit dialog
  via `array_dialog_spec()`/`submit_array_from_dialog()`. New parametric *commands*
  (e.g. a future POLYGON) follow the same pattern when a dialog genuinely fits.
* **Focus + threading.** The autocomplete popup is NoFocus; ARRAY's dialog is the
  existing non-modal `ParameterDialog`. Neither changes the Delete/Esc/click/drag/
  grip/double-click/PR + command-line + DYN routing — verified real-window. No
  data-model change; insert/draw-call baselines unchanged.

## Dynamic Input (DYN / F12) — type at the cursor (Phase 25)

AutoCAD-style Dynamic Input: a cursor-anchored surface so the user types commands
and values AT the crosshair. It is a new **input surface**, not new command logic.

* **Thin over the existing pipeline.** `DynInput` (a frameless `Qt::Tool` window that
  floats over the GL viewport and can take keyboard focus) routes everything through
  the **same** `CommandProcessor`: typed text goes to `submit_line` (the Ph4 coordinate
  parser, Ph6 tokens), and it *reads* `processor_->preview()` + `last_point()` for live
  values. No command/coordinate parsing is duplicated. A `FanoutOutput : CommandOutput`
  fans the prompt/echo to both the bottom command line and DYN, so the two surfaces are
  always in sync and the command line keeps working unchanged.
* **F12 toggle, persisted.** A status-bar `DYN` toggle / F12 turns it on/off; the state
  is saved in `QSettings` (the one persisted UI toggle). OFF == pre-Ph25 behaviour. The
  self-test/dump harness ignores the persisted value and runs DYN-off, so the canonical
  default runtime state (Ph9) is unchanged regardless of a developer's preference.
* **The focus-capture rule (the make-or-break).** When DYN is on, the DYN field **holds
  keyboard focus**, re-acquired only after a *viewport* pick/select (the
  `pickerInteracted` signal → a deferred refocus) — never on a timer, so it can't steal
  keys the user directed at the command line or PR (those, when focused, get the keys
  via normal Qt routing). The global Delete/Backspace erase-selection binding treats the
  DYN field exactly like the command line: it yields only when DYN is *typing* (field
  focused **and** non-empty); an empty focused field still lets Delete erase the
  selection. Esc routes from the field to the viewport's `handle_escape()` (cancels a
  grip drag or the command). Mouse picks / drag-select / grip / double-click are mouse
  events — unaffected by keyboard focus. Verified real-window: every prior gesture still
  works with DYN on.
* **Live dimensional input.** The viewport already computes the constrained (ortho/
  polar/snap) cursor and the `PreviewSpec` in `rebuild_overlay`; it emits the constrained
  cursor (`constrainedCursorMoved`) and DYN derives the display values — length/angle
  (line), radius (circle), width/height (rectangle) — and shows them as placeholders.
  Typing an exact value commits through the **same** command step as a click by
  composing the existing coordinate string (`@len<ang`, `@rad<ang` rel. to centre,
  `@w,h`) from typed-or-live values and submitting it; ORTHO/POLAR/snap are honoured
  because the angle comes from the constrained cursor. Tab switches the two fields.
* **No data-model change; threading intact.** DYN is pure UI glue over the processor;
  the renderer/snapshot are untouched. Insert/draw-call baselines unchanged.

### Command-control keys (Esc / Enter / Space) — the one carve-out

When DYN is on (canvas mode) the on-canvas fields are GL-rendered in the viewport and
their keystrokes are routed by the **app-wide event filter** (`MainWindow::eventFilter`,
the same `qApp` filter as the Delete fix). The dimensional-rubber-band and sub-prompt
routers (`ViewportWindow::dyn_handle_key` / `sub_prompt_handle_key`) return *false* for
Esc and empty Enter "so the caller cancels/ends" — but in canvas mode the classic command
line is hidden, so a fallen-through key reaches no focused widget and is **lost** (the
recurring keyboard-routing bug). Space was never handled at all.

The fix is a single **command-control carve-out** at that one event filter, ahead of the
DYN routers, active only when `processor_->has_active_command()` **and** canvas mode
(`dyn_action_->isChecked()` — so F12-OFF classic is untouched and the command line still
handles these keys). A focused dialog / Properties field keeps its own keys; the hidden
command-line / DYN widgets do not exempt it.

* **Esc** → `viewport_->handle_escape()` (grip-drag cancel, else `processor_->cancel()`),
  then `rebuild_overlay()` clears the rubber-band + fields at once. Esc is **never**
  swallowed by DYN, even with a pending typed value — it cancels the command outright.
* **Enter / Space** (Space ≡ Enter for command flow) → `viewport_->dyn_end_step()`, the
  AutoCAD two-step: a pending typed value commits (`dyn_commit`, or the sub-prompt's
  `submit_line(sub_entry_)`), keeping the command running; with nothing typed it
  `submit_line("")` to end the step (which ends LINE at a next-point prompt).
* **Tab / Shift-Tab** are untouched — they fall through to `dyn_handle_key` and still
  cycle fields. Digit/`.`/`-`/Backspace field editing is unchanged.

### Starting a command cancels the one in progress (one dispatch site)

`CommandProcessor::start_command(alias)` is the single command-start entry. It now runs a
held command's `cancel()` cleanly **first** (dropping its preview/rubber-band and any
pending op-log group) before constructing the new one, instead of letting `active_ =
std::move(cmd)` destroy it mid-flight. The current **selection is preserved** (`cancel()`
never clears it). Ribbon command buttons (`add_cmd`) now call `start_command` directly
rather than `submit_line` — a ribbon click is an unambiguous command start, whereas
`submit_line`'s typed-text path feeds the active command as input (so a mid-command typed
keyword like LINE's `C`/`U` still routes to the command, AutoCAD-correct). `start_command`
is the one place the cancel-current rule lives, so every path that reaches it (ribbon,
shortcut, programmatic) gets the clean hand-off for free.

## Dimension properties in PR — per-dimension overrides (Phase 24)

The Properties palette's deep Dimension group edits **per-dimension overrides**
(this dimension only; the shared DIMSTYLE is untouched — that lives in the DIMSTYLE
dialog), exactly like AutoCAD's PR.

* **Override model = the Ph12 ByLayer/override shape.** `DimOverrides` (in
  `properties.hpp`) is a compact **presence bitmask + values** for the 8 PR-editable
  fields (arrow type/size, dim/ext/text colour, text height, text placement,
  precision). A set bit means "override; use this value", no bit means **ByStyle**.
  It lives inline in `DimData` (the dimension's own arena) — hot structs
  (`LineData` 40 / `CircleData` 32) are untouched; `DimData` grows 56→112 B
  (`DimOverrides` is 40 B), which is fine for the few-and-cold dim arena.
* **One resolution path.** `compute_dim_geometry` (the single place dims resolve
  appearance) applies overrides first via `apply_dim_overrides(style, d.overrides)`
  → an effective style, then the body reads that exactly as before. Override-first-
  else-style, no parallel resolution. So a non-overridden field follows its DIMSTYLE
  (and tracks style edits), while an overridden field stays put — proven by a test
  mirroring the Ph12 colour test.
* **Flows through capture→recreate (undo).** `AddDimensionCommand` gains
  `overrides` (authoritative — `capture_entity` reads it from `DimData`,
  `add_command_to_store` writes it back) plus a `dim_style` snapshot
  (`capture_entity` fills it from the store; **used only for PR effective-value
  display, ignored on recreate**). Edits and reset-to-style ride the Ph22
  `SetPropertyCommand` path → one undo group; Ctrl+Z restores.
* **PR group via the registry (no framework change).** 8 descriptors registered
  with `applies = is_dimension` under group "Dimension" — the per-type registration
  Text/MTEXT established, just more rows. Each row reads the **effective** value
  (override if set, else the captured style) and its ByStyle/Overridden state, and
  offers reset-to-style. Reused `ColorOverride` (its inherit button reads "ByStyle"
  for the dim group); added three editors mirroring existing ones —
  `NumberOverride` (value + ByStyle reset) and `DimArrowTypeCombo` /
  `DimPlacementCombo` (index 0 = ByStyle, like LinetypeCombo's ByLayer). Multi-dim
  selection reuses the `*VARIES*`/set-all aggregation. Covers LINEAR/ALIGNED/RADIUS/
  DIAMETER/ANGULAR; the standalone LEADER's arrow is deferred (Planned).
* **Persistence.** Native **v8** serializes the full override block per DIM line
  (lossless round-trip, proof test); v1–v7 dim lines load as all-ByStyle. **DXF**:
  per-dimension overrides are **not** written — the dim exports referencing its
  style (the override-vs-style distinction is a stated native-only gap); the dim
  still round-trips and renders (LibreCAD-verified).
* **No hot-path regression.** Insert ~31 ns; draw calls 4/6; threading unchanged
  (overrides resolve snapshot-side, read lock-free via the selection summary).

## Linetype pattern rendering & LTSCALE (Phase 23)

Dashed/Center/Hidden linetypes now draw as real dash patterns; Continuous is
unchanged. The linetype was already a stored, PR-editable property (Ph12) — this
phase only renders it, with no data-model change.

* **Derived-not-baked, one shared path.** Dashing happens **at snapshot time** in
  `build_render_snapshot`, never in storage (same parametric rule as tessellation).
  A single walker, `core/linetype.cpp::dash_polyline`, takes any point list and emits
  the "on" sub-segments by **arc-length**. A straight line is a 2-point polyline; a
  curve is its tessellated point list — both go through the same walker, so there is
  one dash path for lines, polylines, circles, arcs, and bulged corners. The dash
  phase carries **continuously across vertices**, so curves dash by true arc-length
  (no restart at each tessellation segment, no bunching at joins) and stay even.
* **Patterns** (`dash_pattern`, drawing units, AutoCAD acad.lin proportions ×10 so
  they read at LTSCALE 1 in a tens-of-units drawing): Dashed `[5, 2.5]`, Hidden
  `[2.5, 1.25]`, Center `[12.5, 2.5, 2.5, 2.5]` (long, gap, dot, gap), Continuous
  `{}`. The effective linetype is ByLayer-resolved per entity (`ResolvedProps`); the
  dashes inherit the entity's resolved colour + lineweight and feed the existing
  `(colour, lineweight)` batches, so the thick-line pipeline thickens each dash and
  **draw calls stay bounded** (one draw per colour/lineweight, as before).
* **Zoom-consistent.** Dashing rides the Ph18 zoom-bucket tessellation: as a curve
  refines on zoom the dash pattern is unchanged (it's parameterized by world
  arc-length, independent of segment count). Pan does not re-dash — dashing lives in
  the geom-dirty rebuild, not per-frame; a zoom-bucket change or an LTSCALE change
  re-derives it, a pan does not.
* **LTSCALE (global linetype scale).** `GeometryStore::ltscale_` (default 1.0)
  multiplies every pattern length before dashing. `build_render_snapshot` takes it as
  a parameter (engine passes `store_.ltscale()`). `LTSCALE`/`LTS` command prompts for
  the factor → `SetLtscaleCommand` → `set_ltscale` + a geom-dirty rebuild, so all
  non-continuous entities re-dash live. It round-trips in the native format (**v7**,
  `LTSCALE` header line; v1–v6 load as 1.0) and DXF (`$LTSCALE` header var). DXF
  export also writes an **LTYPE table** (Continuous/DASHED/HIDDEN/CENTER with element
  lengths mirroring `linetype.cpp`) so the dashes render dashed in other CAD apps
  (LibreCAD-verified). Per-entity scale (CELTSCALE) layers on top — see Phase 33.
* **PR gap closed.** Setting an entity's linetype in the Properties palette (Ph22)
  now visibly dashes it — the write path was already there; this phase makes it draw.
* **No regressions.** No entity-struct change (`LineData` 40 / `CircleData` 32
  unchanged; `ltscale_` is one double on the store); insert ~31 ns; draw calls 4/6;
  dense dashed scene (5000 fine circles) ~6.6 ms/frame.

## Per-entity linetype scale — CELTSCALE (Phase 33)

LTSCALE is the drawing-wide scale; **CELTSCALE** is the per-entity multiplier (AutoCAD's
two knobs). The effective dash scale is `LTSCALE × CELTSCALE`, default 1.0 so existing
drawings are unchanged. (This also corrects the MATCHPROP report's "LTSCALE is global"
note: LTSCALE lives on `GeometryStore`, which is the per-document DocState under multi-doc,
so it is drawing-level **per document**, not process-global.)

* **Sparse, cold storage — hot structs unfattened.** Most entities use 1.0, so CELTSCALE is
  NOT a field on `EntityProps`/`LineData` (that would add 8 bytes to every hot struct).
  Instead it is a default-skipping sparse map on the store,
  `std::unordered_map<key, double> celtscale_` keyed by `(kind<<32)|slot`; absent = 1.0,
  set-to-1.0 erases the entry, `remove()`/`clear()` drop it. `sizeof(LineData)`/`EntityProps`
  are unchanged (asserted in tests). It travels capture→undo→clipboard as a top-level
  `celtscale` field on the dashing Add*Commands (Line/Circle/Arc/Polyline), applied in
  `add_command_to_store` via `set_celtscale`.
* **One multiplication, one path.** `scene_snapshot.cpp` computes
  `ltscale * store.celtscale(h)` at the two `dash_polyline` call sites (line + the curve
  lambda) — the single effective-scale point. The plot path (Ph30) uses the same
  `build_render_snapshot`, so a plot honours `LTSCALE × CELTSCALE` with no fork (verified:
  a plot at LTSCALE 0.5 dashes at the same density as the viewport).
* **PR + MATCHPROP.** A universal `PropertyId::Celtscale` descriptor (`PropEditor::Number`,
  `applies` = the dashing kinds) sits between Linetype and Lineweight in the General group;
  it reads/writes the `celtscale` command field via a `requires{x.celtscale}` accessor (the
  same registry mechanism as every other property — no special path). `MatchSlot::Celtscale`
  (universal) lets MATCHPROP's "Linetype scale" toggle copy it across the dashing kinds.
* **Persistence.** Native bumps to **v12**: decoupled trailing `CELTSCALE <kind> <ordinal>
  <value>` records (only non-default values; older files have none → 1.0), so the per-entity
  record formats are unchanged. DXF uses the standard per-entity **code 48**. Both round-trip
  (tested); older files default to 1.0.
* **Scope (honest).** Only line/circle/arc/polyline dash via `dash_polyline`, so CELTSCALE is
  exposed/effective only there. Dimensions, leaders, and block-internal (INSERT) geometry
  render solid today (pre-existing — they don't route through `dash_polyline`), so CELTSCALE
  is not surfaced for them; block-internal dashing (and thus INSERT CELTSCALE) is deferred.
  Splines dash at LTSCALE only (no Add-command/PR path to set their CELTSCALE).

## Properties palette (PR) — framework, multiplicity, per-type registration (Phase 22)

A live, context-sensitive Properties panel (AutoCAD PR) that reflects the current
selection and edits object properties. It is a **binding layer over fields that
already exist** (Ph12 layer/color/linetype/lineweight, Ph20 text fields) — no new
entity capability, no data-model change.

* **Descriptor registry (the single source of truth).** `core/properties_registry`
  holds one table of `PropertyDescriptor`s. Each declares its `PropertyId`, group
  ("General"/"Geometry"/"Text"), label, editor kind, an `applies(EntityKind)` gate,
  and **read**/**write** functions that operate on a captured `Add*Command`. The
  accessors use C++20 `requires`-expressions, so a property "applies to whichever
  command exposes the member" — no per-type enumeration. Adding a type's deep group
  later is *adding rows to this table*; the engine apply path and the UI renderer do
  not change (the anti-switch-bomb lesson from grips).
* **Multiplicity / aggregation (geometry thread).** `summarize_selection(captured)`
  produces a `SelectionSummary`: the type label ("Line" / "3 Texts" / "Mixed (5)"),
  and the field list. A field is shown only if it applies to **every** selected
  entity — so a homogeneous selection gets universal + type fields, a mixed selection
  gets universal only. Each field carries its value plus a **`varies`** flag set when
  the entities disagree. This runs on the geometry thread and is published in the
  snapshot (`selection_summary`), so the **UI never queries the store** — it reads the
  aggregated view the same lock-free way the renderer reads geometry.
* **Generic write-back (one command, one undo group).** Editing any field emits a
  single `SetPropertyCommand{id, value, group}`. `apply_set_property` looks the id up
  in the registry and applies its `write` to **every** selected entity it applies to,
  via capture/erase/recreate as **one undo group** (the `apply_props_change` pattern,
  so layer/other-props/position are preserved; Ctrl+Z restores). PR never mutates the
  store. The pre-Ph22 `SetEntityLayer/ColorCommand` remain for the ribbon combos; the
  registry's universal writes do the same `EntityProps` mutation, so behaviour isn't
  forked.
* **Generic UI renderer.** `ui/properties_panel` is dumb over the field list: it maps
  each `PropEditor` to a widget (number/text entry, layer/linetype/lineweight/justify/
  attachment combos, a ByLayer-or-pick color control, a read-only label, a content
  editor button, a disabled font combo) and emits `(PropertyId, value)` back. `varies`
  shows as **`*VARIES*`**. It rebuilds only when the summary actually changes. Dark,
  non-native (Fusion palette, Ph11). Dockable; **PR** toggles it via a new
  `ViewControl::open_properties()` → MainWindow callback; a 100 ms timer pushes the
  latest summary to the visible panel.
* **Coverage now.** Universal Layer/Color/Linetype/Lineweight on any selection; a
  read-only Geometry group (line length/ends, circle/arc center+radius, text position);
  and the full **Text/MTEXT** group (contents, height, rotation°, justify, width
  factor, line spacing, defined width, attachment, font). Font is a wired read-only
  combo — the single stroke font is the only option today (stated limitation), so the
  field slots a real font system in later without UI change.
* **Staged (Planned, same pattern):** the deep Dimension group (per-element colors,
  arrow type/size, dimstyle overrides) and numeric geometry editing for line/circle/
  arc/polyline/leader. Universal props already cover their color/layer/linetype/
  lineweight.
* **No regressions.** No struct/data-model change (`TextData` 56 / `MTextData` 80 /
  `LineData` 40 unchanged); insert ~33 ns; native/DXF untouched. `selection_summary`
  is interaction metadata (not in the checksum).

## MATCHPROP / MA — match properties source → targets (Phase 32)

MATCHPROP copies one source entity's properties onto N picked targets. It is a thin
workflow over the **Phase 22 descriptor registry** — there is **one property-write path**
and **one family/category source of truth**; MA adds no entity-write code and no table of
its own.

* **The one write path.** A new registry helper `match_properties(source_cmd, target_cmd,
  filter)` walks `kDescs` and, for each enabled+applicable property, calls the **same**
  per-descriptor `read()`/`write()` the PR palette uses. The engine resolves source/target
  via `pick_nearest`, captures them with `capture_entity`, runs `match_properties`, then
  does the standard **capture → write → erase → recreate** as one undo group — identical to
  `apply_set_property`. No SetProperty/PR path is modified (PR's varies/set-all is untouched).
* **Family classification (the small registry extension).** `family_of(EntityKind)` (in
  `entity_handle.hpp`) tags each kind: **SimpleGeometry** (Point/Line/Circle/Arc/Spline),
  **Text** (Text/MText/Leader/MLeader), **Dimension**, **Polyline**, **Insert**, **Hatch**
  (reserved). Each descriptor is tagged with a `MatchSlot` (`match_slot_for(PropertyId)` —
  one switch next to the descriptors): **universal** slots (Color/Layer/Lineweight/Linetype,
  `any_kind`) copy across ANY kinds; **family-scoped** slots (Text, Dimension) copy only when
  `family_of(source)==family_of(target)` AND the descriptor applies to both kinds. Content
  (TextContent), placement (TextRotation), the MTEXT wrap box (MtWidth), and read-only
  geometry are slot `None` — never matched (AutoCAD-clean).
* **ByLayer/ByBlock as state.** Copying is free of resolution: the colour/linetype/lineweight
  descriptors carry `{flag = by_layer, literal}` and `write()` only assigns the literal when
  the flag is clear — so a ByLayer source yields a ByLayer target, never the source's resolved
  colour (Ph12 discipline, preserved automatically).
* **The command.** `MatchPropCommand` (alias **MA**/PAINTER) is pick-based like JOIN
  (`wants_selection()` stays false; clicks arrive as points; the engine resolves entities).
  State machine: source pick → `MatchPropPickSourceCommand` (engine captures the source on
  the geometry thread — the UI never reads the store); then a target loop submitting
  `MatchPropApplyCommand{point, radius, filter, group}` with a **fresh undo group per target**
  (`CommandContext::new_group()`) so each match undoes in reverse. Targets respect the same
  `selectable()` gate (off/frozen/locked excluded); the source itself is skipped.
  **Noun-verb:** running MA with a selection already active uses the first selected entity as
  the source (`MatchPropSourceFromSelectionCommand`, reducing the selection to it) and goes
  straight to the destination loop — the JOIN convenience. **Sub-entity picking:** a dimension
  is pickable by its measured **text** label (the kernel's `closest_point` for a Dimension
  includes the label's bbox, not just the ext/dim/arrow lines), because the text is part of
  the dim entity — so a click on the text resolves to the dimension at both source and target.
* **Settings + cursor (UI).** Typing `S` opens a dark modal (the existing Fusion palette) of
  category checkboxes (Basic: Color/Layer/Lineweight/Linetype, + reserved LTScale/PlotStyle;
  Special: Text/Dimension, + reserved Hatch/Polyline); all on by default, persisted in
  QSettings for the session. The choice is read for every target apply. A drawn paintbrush
  cursor signals match mode while picking. Both reach the command via three `ViewControl`
  hooks (`match_filter`/`match_settings_dialog`/`set_match_cursor`) the `ViewportWindow`
  implements and forwards to the `MainWindow` — the command/geometry layers never touch Qt.
* **Honest model limits (stated).** LTSCALE is a global (not a per-entity property) and plot
  style/hatch are unmodelled, so those Settings toggles are present for AutoCAD parity but
  gate no descriptors (disabled in the dialog). Polyline has no type-specific registry
  descriptor yet. (Leader/MLeader label text props **are now registry-exposed** — see
  "Leader / MLeader labels are text-family" above — so MATCHPROP copies the label
  font/height across the whole text family.) Copying the **dimstyle index** itself would need
  a new registry descriptor (MA copies the per-dim *overrides* the registry exposes); deferred.

## Editable text — double-click & TEXTEDIT (Phase 21)

The text data model was already edit-ready (content is a discrete stored field,
layout computed-not-baked, Ph20). This phase adds only the **interaction** — no
data-model change.

* **One edit command.** `EditTextContentCommand{at, pick_radius, content, group}`
  changes the content of the nearest editable text-bearing entity (TEXT / MTEXT /
  QLEADER label) at `at`. The engine's `apply_text_edit` finds it (by AABB, gated by
  `selectable()` so locked/off/frozen text can't be edited), then **captures the
  entity, changes only its content, and recommits as one undo group** — exactly the
  `apply_grip_commit` pattern, so layer/properties/position are preserved (not a
  delete+recreate) and Ctrl+Z restores the prior string. Layout re-computes from the
  new content at the next snapshot (so an edited MTEXT re-wraps automatically).
* **Double-click gesture.** The snapshot carries `text_edit_targets` — for every
  editable TEXT/MTEXT/QLEADER label, its handle, AABB, anchor, height, multi-line
  flag, and live content. The UI caches these and, on `mouseDoubleClickEvent`,
  hit-tests them (reusing the pick aperture, DPR-aware) without touching the store;
  on a hit it opens an editor pre-filled from the snapshot content. Double-click is
  idle-only, so single-click select and grip-grab are unaffected.
* **Editor UI.** A small **modal popup** (an in-canvas overlay is awkward over the
  QWindow GL surface — stated choice), themed by the app-wide Fusion + dark palette
  (Ph11): a `QLineEdit` for single-line TEXT, a `QPlainTextEdit` (multi-line) for
  MTEXT/QLEADER. On confirm it submits `EditTextContentCommand`; Esc/Cancel changes
  nothing. The UI never touches the store — the edit rides the command queue.
* **TEXTEDIT / DDEDIT (ED).** The scriptable/keyboard path: pick a text entity, type
  the new content, submit the same `EditTextContentCommand` — one code path, same
  one-undo-group commit. Lets a script/AI edit text via a command, not a gesture.
* **No regressions.** No struct/data-model change (`TextData` 56 / `MTextData` 80 /
  `MLeaderData` 96 unchanged); insert ~36 ns; native/DXF untouched (content was always
  stored). `text_edit_targets` is interaction metadata (not in the checksum).

## MTEXT (paragraph text) & QLEADER (editable leaders) (Phase 20)

Two new entities, both **property-bearing** so the next phase's Properties palette
can edit their fields without a rebuild.

* **Shared, stored-not-baked text model.** `MTextBlock` (in the tiny standalone
  `core/mtext_block.hpp`, so commands/IR can use it without pulling in the store)
  holds the **discrete, queryable** fields: insertion `pos`, defined wrap `width`,
  `height`, `rotation`, `width_factor`, `line_spacing`, `attach` (TL..BR), and the
  content range in the shared string pool. **Layout is computed at snapshot time**
  from these fields by the single `text/mtext.cpp::layout_mtext` (greedy word-wrap to
  the width using the stroke-font metrics × `width_factor`, line stacking by
  `line_spacing`, anchoring by `attach`, rotation) — nothing about glyph placement is
  stored. The same function feeds the snapshot, the AABB (`entity_bounds`), the pick
  outline (`native_kernel`), and the grips, so there is **one** text-layout path.
  Per-character inline formatting (bold / mid-string colour or height) is **Planned**,
  not faked; the paragraph-level fields are what the PR phase edits first.
* **MTEXT entity** (`MTextData = MTextBlock + props`, own arena). Command `MT/MTEXT/T`
  picks two corners (insertion + wrap width) then the text. Grips: insertion (move) +
  **width** (drag re-wraps live — the layout follows the field). Selectable by its
  box, layer-aware, erasable, undoable.
* **QLEADER entity** (`MLeaderData`, own arena): leader vertices in the shared
  polyline pool (vertex 0 = arrow tip, last = landing), a dimstyle arrow (reusing the
  dimension arrowhead machinery), and an **owned** `MTextBlock` label. **Association is
  ownership** — the label lives inside the leader, so moving the leader moves the text;
  there is no cross-entity reference to dangle. Command `LE/QLEADER/QL`: arrow → vertices
  (Enter to finish) → annotation. Grips: arrow tip, each vertex, and the text position;
  dragging the landing vertex carries the label with it. The older simple `LEADER`
  entity/command is kept for file compatibility.
* **Persistence.** Native format **v6** adds `MTEXT`/`MLEADER` records (content
  newlines escaped as `\n`); v1–v5 files still load (no mtext/mleader). DXF writes
  standard `MTEXT` group codes (10/20, 40, 41, 71, 50, 1; hard breaks as `\P`) and
  round-trips them; a QLEADER is written as a readable `LEADER` polyline **+** an
  `MTEXT` label — full MLEADER-block fidelity is a **stated gap** (the native format
  preserves the association losslessly; through DXF it returns as a leader line + an
  MTEXT). LibreCAD-verified.
* **Footprint.** `MTextData` 80 B, `MLeaderData` 96 B, each in its own arena; the
  hot `LineData` (40 B) / `DimData` (72 B) are untouched and the insert baseline is
  unchanged (~36 ns/line). Strings share the existing char pool.
* **PR-readiness (next phase).** The Properties palette will expose, per MTEXT/
  QLEADER: content, text height, width factor, line spacing, attachment, defined
  width, rotation, and colour (ByLayer/override) — all already stored as discrete
  fields, with layout recomputed on edit.

## HiDPI / device-pixel-ratio correctness (Phase 19)

Lines rendered correctly on a normal monitor but ~2× too thin on a HiDPI laptop.

* **Root cause.** The GL framebuffer is correctly sized in **physical** pixels
  (`width()*devicePixelRatio()`), but the lineweight density used
  `QScreen::physicalDotsPerInch()/25.4`, which is a **logical** pixel density (Qt
  derives `physicalDotsPerInch` from device-independent geometry). It therefore omits
  the `×dpr`, so on a 2× display every line was half its intended physical thickness;
  fixed-size screen markers (grips, snap glyph, crosshair/pick-box) were undersized for
  the same reason.
* **Fix (renderer owns it, one place).** `ViewportRenderer::set_device_pixel_ratio(dpr)`
  is pushed every frame from `QWindow::devicePixelRatio()`. The effective lineweight
  density is `device_px_per_mm_ × dpr`, and all screen-space marker sizes (grip square,
  snap marker + stroke, crosshair pick-box) are scaled by `dpr`. So a 0.25 mm line — and
  a grip square — are the **same physical size** on a 1× monitor and a 2× laptop;
  AutoCAD's hairline (Ph15) is preserved, now equally thin everywhere. The pick/grab/snap
  apertures are likewise `×dpr` (consistent physical pick tolerance). Default `dpr=1`
  leaves normal monitors and the offscreen harness unchanged; the harness verifies a
  given mm renders ~2× thicker at `dpr=2`.
* **Display change.** Both the DPI and the DPR are pushed every frame from the live
  `QScreen`, so dragging the window between the laptop panel and an external monitor
  self-corrects on the next frame (no explicit screen-change signal needed).

## Build / phase status

* **Phase 1 — complete:** cross-platform CMake build; empty "Musa CAD" Qt6
  window; sanitizer + warnings infrastructure; all five targets established.
* **Phase 2 — complete:** double-precision math types; generational handles; SoA
  `GeometryStore`; `IGeometryKernel` + `NativeKernel2D`; `MpscQueue`,
  lock-free `TripleBuffer`, and the `GeometryEngine` worker. 27 unit tests pass
  under ASan/UBSan and ThreadSanitizer; header-hygiene enforced.
* **Phase 3 — complete:** backend-agnostic GPU abstraction + OpenGL 4.6 backend;
  camera (pan/zoom-about-cursor), decade-stepping grid, instanced line/point
  rendering, stroke-font FPS overlay; render-thread viewport hosted in the main
  window. 39 unit tests + an offscreen real-GPU verification harness pass.
* **Phase 4 — complete:** table-driven command line (`L/C/PL/A/REC/U/ERASE/ZOOM`),
  per-command state machines, absolute/relative/polar coordinate parsing,
  bottom-docked command widget with history + ENTER-repeat + ESC, live
  status-bar coordinate readout, and a geometry-thread undo model. 60 unit tests
  pass (ASan + TSan); full command→queue→store→snapshot round-trip verified.
* **Phase 5 — complete:** uniform spatial index (OSNAP + ERASE-pick); OSNAP
  (endpoint/midpoint/center/intersection/nearest) published via the snapshot;
  render-side crosshairs; snap markers; ortho/polar/grid-snap; F3/F7/F8/F9/F10
  and Ctrl+Z/Ctrl+Y; status-bar indicators; toolbar. 80 unit tests pass under
  ASan + TSan.

* **Phase 6 — complete:** AutoCAD-2023-style Ribbon frame (QAT + tabbed panels +
  file/layout tabs + status-bar mode toggles), full-crosshair-with-pick-box
  cursor, and registry-driven command autocomplete. 85 unit tests pass under
  ASan + TSan; presentation-only (no command logic changes).

* **Phase 7 — complete:** render-side live preview (rubber-banding) for all draw
  commands; AutoCAD selection (single-pick / window / crossing, Shift-add,
  Esc-clear, select-all) with highlight; Modify panel — MOVE, COPY, MIRROR,
  OFFSET, and TRIM (line subset) with cursor preview/ghost and undo/redo;
  selection-driven button enablement. 100 unit tests pass under ASan + TSan.

* **Phase 8 — complete:** empty Model space on launch (demo behind `MUSACAD_DEMO`);
  Delete/Backspace erases the selection (one undo group); full OSNAP set
  (endpoint/midpoint/center/node/quadrant/intersection/perpendicular/tangent/
  centroid/nearest) with distinct render-side markers, a toggleable running-osnap
  mask, and documented precedence. 110 unit tests pass under ASan + TSan.

* **Phase 9 — complete:** Delete/Backspace erase fixed via an app-wide event
  filter (verified on the real window); snap markers strengthened (bright/bold/
  larger, centralized theme); rollover hover-highlight (render-side, three
  distinct states). 112 unit tests pass under ASan + TSan.

* **Phase 10 — complete:** the Modify suite — ROTATE, SCALE, ARRAY (rect+polar),
  EXTEND, TRIM (exact, line entities), FILLET (line/line incl. tangent arc),
  CHAMFER — on shared `NativeKernel2D` intersection primitives, each one undoable
  group, wired to ribbon + alias. 124 unit tests pass under ASan + TSan.

* **Phase 11 — complete:** persistence — native `.musa` save/open (lossless,
  proven round-trip), DXF import/export (R2000; LibreCAD-verified), dirty
  tracking, all on the geometry-thread message pipeline, fail-safe on bad input.
  147 unit tests pass under ASan + TSan.

* **Phase 12 — complete:** layers & the ByLayer/override property model —
  layer table + CRUD, effective-property resolution, off/frozen skip + locked
  inert (render & pick), per-colour batched rendering, the Layer Manager UI, and
  native-v2 + DXF LAYER-table persistence. 159 unit tests pass under ASan + TSan.

* **Polyline arc segments (bulges) — complete:** filleted rectangle/polyline corners
  are now true arc segments (per-vertex bulge), so DIMRADIUS/DIMDIAMETER can dimension
  them. Tessellation is zoom-adaptive and shared; geometry stays parametric; native v5
  + DXF code 42 round-trip (LibreCAD-verified). `PolylineData` 20→24 B; insert baseline
  unchanged. 204 tests (dev) / 203 (TSan) pass under ASan + TSan.
* **Phase 27 — complete:** DWG import/export via an EXTERNAL converter (FreeCAD
  pattern). `ui/dwg_converter` discovers ODA File Converter / LibreDWG at runtime and
  invokes it as a subprocess off the UI thread; import reuses the existing fail-safe
  DXF load and surfaces an itemised gap catalog (+ a `.import.log`); export reuses the
  DXF exporter then converts (two-stage lossy, stated). LGPL-clean: no GPL/converter is
  linked, bundled, or in the build (process boundary). DWGIN/DWGOUT + ribbon. No
  data-model change. 234 tests (dev) / 233 (TSan); real-window selftest via a mock
  converter (graceful-degradation, discovery, import+catalog, export round-trip).
* **Phase 26 — complete:** DYN autocomplete (the Ph6 registry suggestions anchored
  at the cursor field, one source, NoFocus popup). Draw/transform stay interactive
  (ribbon starts the command, pick on screen — the AutoCAD model; the cursor value
  surface is DYN, not a modal); CIRCLE gained the radius/[Diameter] option keyword
  (identical in command line + DYN). ARRAY keeps its Ph11 multi-parameter dialog. No
  data-model change. 234 tests (dev) / 233 (TSan); real-window selftest (autocomplete,
  ribbon-starts-interactive, [Diameter] option, typed radius).
* **Phase 25 — complete:** Dynamic Input (DYN / F12). A cursor-anchored surface
  (`DynInput`) over the existing CommandProcessor — typed text routes through
  `submit_line` (Ph4 parser, Ph6 tokens), a `FanoutOutput` mirrors the prompt to the
  bottom command line, and live dimensional input (length/angle, radius, w/h) reads the
  preview's constrained cursor and commits via composed coordinate strings. Persisted
  F12 toggle; the focus rule keeps Delete/Esc/click/drag/grip/double-click/PR+command-
  line typing all working with DYN on (real-window verified). No data-model change. 233
  tests (dev) / 232 (TSan); real-window selftest.
* **Phase 24 — complete:** dimension properties in PR (per-dimension overrides). A
  compact `DimOverrides` bitmask+values (Ph12 ByLayer/override shape) in `DimData`,
  resolved override-first in the single `compute_dim_geometry` path; 8 PR rows via the
  registry (arrow type/size, dim/ext/text colour, text height/placement, precision)
  with ByStyle-or-Overridden + reset-to-style; one undo group; multi-dim varies/set-all;
  native v8 lossless (DXF override-vs-style native-only). Hot structs unchanged. 232
  tests (dev) / 231 (TSan); real-window selftest (set/reset/undo/multi).
* **Phase 23 — complete:** linetype pattern rendering + LTSCALE. One arc-length dash
  walker (`core/linetype`) dashes lines and curves through the same path (phase carries
  across tessellation vertices; zoom-consistent; bounded draw calls). Patterns follow
  acad.lin; LTSCALE (command + native v7 + DXF $LTSCALE + LTYPE table, LibreCAD-verified)
  scales them and re-dashes live. Derived-not-baked; no entity-struct change. Closes the
  Ph22 PR-linetype gap. 229 tests (dev) / 228 (TSan); real-window selftest.
* **Phase 22 — complete:** Properties palette (PR). A generic descriptor registry
  (`core/properties_registry`) drives a geometry-thread `summarize_selection` (values
  + `varies`, published in the snapshot) and a single `SetPropertyCommand` write path
  (one undo group over the whole selection). Dark dockable panel handles all four
  multiplicity cases (none/one/many-same/mixed). Universal Layer/Color/Linetype/
  Lineweight + read-only Geometry + full Text/MTEXT group; Dimension/numeric-geometry
  groups staged behind the same registration. No data-model change. 221 tests (dev) /
  220 (TSan); real-window selftest verifies multiplicity + observed store edits + undo.
* **Phase 21 — complete:** editable text — double-click a TEXT/MTEXT/QLEADER label
  to edit its content (dark modal editor pre-filled from the snapshot) + TEXTEDIT/
  DDEDIT/ED command (scriptable path). Both submit one `EditTextContentCommand` that
  changes only the content as one undo group (layer/props/position preserved), with
  layout recomputed at snapshot. No data-model change; 216 tests (dev) / 215 (TSan);
  observed-outcome self-test (store content BEFORE→AFTER, undo restores).
* **Phase 20 — complete:** MTEXT (paragraph text, word-wrap, attachment/spacing/
  width-factor fields, width + insertion grips) and QLEADER (arrow + leader vertices +
  owned MTEXT label; arrow/vertex/text grips; the label moves with the leader). Both
  store discrete editable fields with layout computed at snapshot (PR-ready); one shared
  text-layout path; native v6 + DXF MTEXT/LEADER round-trip (LibreCAD-verified, MLEADER
  fidelity gap stated). New structs in their own arenas (MTextData 80 B, MLeaderData
  96 B); LineData/DimData unchanged; insert ~36 ns. 213 tests (dev) / 212 (TSan) pass.
* **Phase 19 — complete:** full dimension grip set (both ext-line origins, both
  dim-line feet, offset midpoint — grabbable anywhere, freely placeable; no edit-path
  fork, `DimData` unchanged) + HiDPI device-pixel-ratio lineweight fix (lineweights and
  screen markers are the same physical size on 1× and 2× displays; the renderer scales
  by `dpr`, self-correcting on monitor change). 206 tests (dev) / 205 (TSan) pass; insert
  ~40 ns/line, draw calls bounded.
* **Phase 17 — complete:** grip editing (direct manipulation) — a per-entity grip
  system (`core/grips`) with grips for line/circle/arc/polyline/rectangle/text and the
  rich dimension set (dim-line offset + def-point re-measure). A grip drag is a
  transient preview computed on a temp store (zero store/op-log churn mid-drag),
  commits as one undo group on release, Esc cancels; ORTHO/POLAR/OSNAP honored; grip
  squares batched (draw calls bounded). Geometry stays parametric; grip-edited drawings
  round-trip native + DXF. 199 tests (dev) / 199 (TSan) pass under ASan + TSan.
* **Phase 16 — complete:** zoom-adaptive curve tessellation (smooth arcs/circles at
  any zoom; re-tessellate on a zoom-bucket change, never on pan; curves stay
  parametric, work capped at 8192 segs/curve), round thick-line joins/caps via a
  capsule-SDF fragment shader (no gaps/notches, no extra draw call), and full
  dimension placement preview that rubber-bands the live dimension to the cursor for
  every dim type (zero store/op-log mutation during the drag). 195 tests (dev) / 194
  (TSan) pass under ASan + TSan.
* **Phase 15 — complete:** object-aware dimensioning (select the circle/arc/line/
  segment; the geometry thread reads its intrinsic geometry), the smart all-in-one
  **DIM** command (hover previews + dispatches by entity kind to the shared
  machinery), and AutoCAD-accurate DPI-anchored lineweight (`px = mm × DPI/25.4`,
  Default = 1px hairline). Dims store def points only — deleting the source entity
  never dangles them. No shared struct grew; native/DXF formats unchanged. 188
  tests (dev) / 187 (TSan) pass under ASan + TSan.
* **Phase 14 — complete:** real lineweight rendering (screen-space expanded
  quads, LWDISPLAY toggle), solid filled arrowheads (filled/open/tick/dot),
  DIMSTYLE per-element colours, and the remaining dimension types (radius/diameter/
  angular) + leaders. Native v4 + DXF round-trip; `docs/AUTOCAD_CONFIG.md` catalogs
  the full config roadmap. 177 unit tests pass under ASan + TSan.
* **Phase 13 — complete:** vector text rendering (single-stroke font), the TEXT
  entity, and dimensions (DIMLINEAR + DIMALIGNED solid, others staged) with a real
  DIMSTYLE table, arrowheads, the Annotate ribbon, a dimstyle dialog, and native-v3
  + DXF persistence. 172 unit tests pass under ASan + TSan.

## Known deferrals

* **Vertex-pool compaction** — removed polylines/splines leave vertices in the
  append-only pool (snapshots already ignore them; memory reclaim is deferred).
* **ERASE-Last re-tracking** — an entity restored by undoing an erase is not
  re-tracked for a later `ERASE Last`.
* **Vulkan backend** — the GPU seam exists; only the OpenGL 4.6 backend is
  implemented.
* **Adaptive spatial-index cell size** — currently a fixed uniform grid.
* **TRIM of arcs/circles/curves** — only line trimming is implemented; curve
  trimming and the broader Modify suite (ROTATE/SCALE/EXTEND/FILLET/...) are
  Planned (Phase 8) in COMMANDS.md.
* **In-command "Select objects:" prompting** — Modify commands consume a
  pre-existing selection (buttons gate on it); selecting *during* a Modify
  command is deferred.
* **Snap tooltip text label** (e.g. "Endpoint" next to the marker) — deferred;
  the render-side stroke font currently has only digits + a few glyphs, so a full
  alphabet (or a Qt text overlay) is needed first.
* **Windows** — build is configured per docs/BUILD.md but verified only on Linux.
