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

## Object-aware dimensioning & AutoCAD-accurate lineweight (Phase 15)

* **AutoCAD-accurate lineweight (corrected).** The Phase-14 mapping
  (`px = max(1.5, mm·6)`, a magic constant) is replaced by a DPI-anchored one:
  `px = max(1.0, mm × device_px_per_mm)` with `device_px_per_mm = screen_DPI / 25.4`.
  The framebuffer is in physical device pixels, so Qt's `QScreen::physicalDotsPerInch`
  already folds in the device-pixel-ratio; the viewport sets it each frame, and the
  renderer defaults to a 96-DPI assumption (~3.78 px/mm) for offscreen/test use.
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
