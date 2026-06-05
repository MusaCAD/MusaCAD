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
  hardcoded per widget. Icons are drawn at runtime (`command_icons.cpp`), so no
  binary assets ship.
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
* **Windows** — build is configured per docs/BUILD.md but verified only on Linux.
