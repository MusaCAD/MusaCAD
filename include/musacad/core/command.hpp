// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "musacad/core/entity_handle.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/mtext_block.hpp"
#include "musacad/core/table_types.hpp"
#include "musacad/core/named_view.hpp"
#include "musacad/core/text_style.hpp"
#include "musacad/core/units.hpp"
#include "musacad/core/page_setup.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/properties_palette.hpp"

namespace musacad::core {

// Commands are the ONLY thing that crosses the UI -> geometry boundary (via the
// MPSC queue). The geometry thread applies them to the store and maintains the
// undo log. Entity-creating commands carry a `group` id: all messages emitted by
// one command-line invocation share a group, so a single command-line undo
// removes the whole invocation. `group` defaults to 0 for ad-hoc submitters.

/// Which entities ERASE targets (selection/picking arrives in Phase 5).
enum class EraseScope : std::uint8_t { Last, All };

/// REVCLOUD's Object option: convert the curve under `pick` into a revision cloud with
/// lobes of ~`arc_len` chord, replacing it (as AutoCAD does) as one undo group.
struct RevcloudObjectCommand {
    Vec2 pick;
    double pick_radius = 0.0;
    double arc_len = 0.5;
    std::uint64_t group = 0;
};

/// REVCLOUD's "Reverse direction": flip every lobe of the selected cloud polylines
/// (bulge signs), which turns outward lobes inward and back.
struct RevcloudReverseCommand {
    std::uint64_t group = 0;
};

/// EXPLODE (AutoCAD): break every selected compound object into its components, as one
/// undo group. What each kind becomes follows AutoCAD's table: a polyline into lines and
/// arcs, a block reference one level down, a dimension or leader into lines, solids and
/// text, a hatch into pattern lines (a SOLID hatch into its boundary loops), MTEXT into
/// one TEXT per line, a table into lines and text. Simple objects are left alone and
/// counted in the report.
struct ExplodeSelectionCommand {
    std::uint64_t group = 0;
};

/// PURGE (AutoCAD): drop symbol-table entries nothing refers to. Today that means
/// unused LAYERS, which is what an imported drawing accumulates dozens of; dimension
/// styles, text styles and block definitions are not purgeable yet (see docs/COMMANDS.md).
/// Reports how many went, so an empty purge says so rather than looking like success.
struct PurgeCommand {
    std::uint64_t group = 0;
    /// What to purge: 0 all, 1 blocks, 2 dimstyles, 3 groups, 4 layers, 5 table styles,
    /// 6 image definitions.
    std::uint8_t what = 0;
};

/// UNITS: set how lengths and angles are displayed (stored with the drawing).
struct SetUnitsCommand {
    DrawingUnits units;
};
/// AUDIT: validate references and structure; with `fix`, repair what can be repaired.
struct AuditCommand {
    bool fix = false;
};
/// BLOCK: make the CURRENT SELECTION a block definition named `name` with base point
/// `base`, and replace the originals by one INSERT of it (in place). Kinds the block
/// content cannot hold are left in place and counted.
struct DefineBlockCommand {
    std::string name;
    Vec2 base{};
    std::uint64_t group = 0;
};
/// INSERT by block NAME (the command line's form).
struct InsertBlockCommand {
    std::string name;
    Vec2 pos{};
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation = 0.0;
    std::uint64_t group = 0;
    std::vector<std::string> attribs; ///< attribute values in the block's attdef order
};
/// WBLOCK: write a block definition (or the whole drawing when `name` is empty) to a
/// .musa file; a block's geometry is written with its base point at the origin.
struct WriteBlockCommand {
    std::string name;
    std::string path;
};
/// REGEN: rebuild and republish the scene.
struct RegenCommand {};

/// PEDIT: one edit on the polyline under `pick` (a line or arc there is turned into a
/// polyline first, as AutoCAD offers). `op`: 0 Close, 1 Open, 2 Reverse, 3 Decurve,
/// 4 Spline (a fit spline through the vertices), 5 insert a vertex at p1, 6 delete the
/// vertex nearest p1, 7 move the vertex nearest p1 to p2. One undo group each.
struct PeditCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    std::uint8_t op = 0;
    Vec2 p1{};
    Vec2 p2{};
    std::uint64_t group = 0;
};

/// STYLE: add or replace a text style; optionally make it current.
struct SetTextStyleCommand {
    TextStyle style;
    bool make_current = true;
};
/// STYLE: make an existing style current (by name).
struct SetCurrentTextStyleCommand {
    std::string name;
};

/// ALIGN (AutoCAD): move, rotate and optionally uniformly scale the selection so that
/// `src1` lands on `dst1` and the direction src1->src2 lines up with dst1->dst2. With
/// `scale` true the distance dst1..dst2 also sets the size, which is how a detail is
/// fitted between two known points in one step.
struct AlignSelectionCommand {
    Vec2 src1;
    Vec2 dst1;
    Vec2 src2;
    Vec2 dst2;
    bool scale = false;
    std::uint64_t group = 0;
};

/// LENGTHEN (AutoCAD): change the length of the open curve under `pick`. The END NEARER
/// the pick is the one that moves, which is how AutoCAD decides. `mode` selects what
/// `value` means: Delta adds to the current length, Percent sets it to a percentage of
/// it, Total sets it outright.
struct LengthenCommand {
    enum class Mode : std::uint8_t { Delta = 0, Percent = 1, Total = 2 };
    Vec2 pick;
    double pick_radius = 0.0;
    Mode mode = Mode::Total;
    double value = 0.0;
    std::uint64_t group = 0;
};

/// BREAK (AutoCAD BR): remove the piece of the curve under `pick` that lies between
/// `p1` and `p2`. When the two points coincide it is BREAK AT POINT (AutoCAD's BREAKATPOINT):
/// the curve is split in two with no gap. A circle becomes a single arc, since a circle
/// with a piece missing is an arc.
struct BreakCommand {
    Vec2 pick;
    double pick_radius = 0.0;
    Vec2 p1;
    Vec2 p2;
    std::uint64_t group = 0;
};

/// DIVIDE and MEASURE (AutoCAD): place POINT entities along the curve under `pick`.
/// `segments` > 0 selects DIVIDE (that many equal parts); otherwise `distance` selects
/// MEASURE (a point every `distance` along the curve). The curve itself is not changed.
struct DividePathCommand {
    Vec2 pick;
    double pick_radius = 0.0;
    int segments = 0;
    double distance = 0.0;
    std::uint64_t group = 0;
};

/// A construction line (AutoCAD XLINE / RAY): base point, unit direction, and whether
/// it is a semi-infinite RAY. Excluded from bounds and the spatial index; the renderer
/// clips it to the viewport each frame.
struct AddXlineCommand {
    Vec2 base;
    Vec2 dir{1.0, 0.0};
    bool ray = false;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
};

/// An ellipse / elliptical arc (AutoCAD ELLIPSE): centre, major half-axis vector, ratio
/// (minor/major, 0..1], counter-clockwise parameter range (full = 0..2pi).
struct AddEllipseCommand {
    Vec2 center;
    Vec2 major{1.0, 0.0};
    double ratio = 1.0;
    double start = 0.0;
    double end = 6.283185307179586;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
};

/// A SPLINE entity: a clamped B-spline with uniform interior knots through these
/// CONTROL points (the SPLINE command's Fit method interpolates the fit points into
/// control points before submitting, see core/spline_eval.hpp).
struct AddSplineCommand {
    std::vector<Vec2> control_points;
    std::uint32_t degree = 3;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
};

/// VIEW Save: store (or replace) a named view. VIEW Delete: remove one by name.
struct SaveNamedViewCommand {
    NamedView view;
};
struct DeleteNamedViewCommand {
    std::string name;
};

/// GROUP: make the CURRENT SELECTION a group (unnamed groups get "*A<n>").
struct CreateGroupCommand {
    std::string name;
    std::string description;
};
/// UNGROUP: dissolve the group containing the entity under `pick`, or the group named.
struct UngroupCommand {
    std::string name;
    Vec2 pick{};
    double pick_radius = 0.0;
    bool by_name = false;
};
/// PICKSTYLE: whether picking a group member selects the whole group.
struct SetPickStyleCommand {
    bool group_select = true;
};

/// A POINT entity (AutoCAD POINT). Points are already stored, drawn, picked, bounded
/// and persisted; this is the command that creates one, which is what the POINT command
/// and DIVIDE/MEASURE all needed.
struct AddPointCommand {
    Vec2 p;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
};

// Add* commands carry an optional EntityProps. Empty => the engine stamps the
// current layer (a fresh user draw); set => exact props (capture/undo/move,
// preserving layer + overrides).
// `celtscale` is the per-entity linetype scale (AutoCAD CELTSCALE, default 1.0); it
// round-trips capture/undo/clipboard for the linetype-dashing entity kinds. The store
// holds it sparsely (not in the hot data struct), so it travels on the command, not props.
struct AddLineCommand {
    Vec2 a;
    Vec2 b;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    double celtscale = 1.0;
};

struct AddPolylineCommand {
    std::vector<Vec2> points;
    bool closed = false;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    /// Per-vertex arc bulges (b = tan(theta/4); 0 = straight). Empty = all straight.
    std::vector<double> bulges = {};
    double celtscale = 1.0;
};

struct AddCircleCommand {
    Vec2 center;
    double radius = 0.0;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    double celtscale = 1.0;
};

struct AddArcCommand {
    Vec2 center;
    double radius = 0.0;
    double start_angle = 0.0; ///< radians, CCW
    double end_angle = 0.0;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    double celtscale = 1.0;
};

struct EraseCommand {
    EraseScope scope = EraseScope::Last;
    std::uint64_t group = 0;
};

/// Undo the most recent command group (command-line `U` / Ctrl+Z).
struct UndoLastGroupCommand {};

/// Redo the most recently undone command group (Ctrl+Y).
struct RedoLastGroupCommand {};

/// Undo a single most-recent op (a command's in-progress `[Undo]` option).
struct UndoLastOpCommand {};

/// Updates the geometry thread's notion of the cursor so it can compute the
/// active object-snap and publish it in the snapshot. Lightweight and coalesced;
/// never blocks the UI.
struct SetCursorCommand {
    Vec2 world;
    double pick_radius = 0.0; ///< snap aperture in world units (0 disables)
    bool osnap = true;
    std::uint32_t snap_mask = 0xFFFFFFFFu; ///< running-osnap mask (which types are on)
    Vec2 from{};                           ///< previous command point (deferred snaps)
    bool has_from = false;
};

/// Erases the entity nearest `world` within `pick_radius` (cursor-pick, via the
/// shared spatial index). Undoable.
struct ErasePickCommand {
    Vec2 world;
    double pick_radius = 0.0;
    std::uint64_t group = 0;
};

// ---------------------------------------------------------------------------
// Phase 7 -- selection (the rubber-band visual is render-side; these mutate the
// geometry-side selection set) and modify operations on the selection.
// ---------------------------------------------------------------------------

/// Select the entity nearest `world` within `radius` (single-click pick).
struct SelectPickCommand {
    Vec2 world;
    double radius = 0.0;
    bool additive = false; ///< Shift: add to selection rather than replace
    /// True while a command is at its "Select objects:" prompt: the engine then echoes
    /// AutoCAD's "N found" / "N found, M total" through the status channel. Off for idle
    /// selection, where that would be noise.
    bool announce = false;
};

/// Box select. `crossing` false = window (entities fully enclosed), true =
/// crossing (entities touched/crossed).
struct SelectWindowCommand {
    Vec2 min;
    Vec2 max;
    bool crossing = false;
    bool additive = false;
    bool announce = false; ///< see SelectPickCommand::announce
};

struct SelectAllCommand {};
struct ClearSelectionCommand {};

/// Erases all currently-selected entities as one undo group (Delete key).
struct EraseSelectionCommand {
    std::uint64_t group = 0;
};

/// Translate all selected entities by `delta` (one undo group).
struct MoveSelectionCommand {
    Vec2 delta;
    std::uint64_t group = 0;
};

/// DIMCONTINUE / DIMBASELINE (issue #28). Placement helpers over the EXISTING
/// Linear/Aligned types rather than new DimTypes: they only decide where the next
/// dimension's def points and dimension line go, relative to the previous dimension.
/// `baseline` false = continue the chain from the previous second extension line;
/// true = stack from the previous FIRST extension line, offset by the baseline spacing.
struct ChainDimensionCommand {
    Vec2 at;              ///< the new second extension-line origin
    bool baseline = false;
    std::uint64_t group = 0;
};

/// Inquiry (issue #30). Read-only queries resolved on the geometry thread and reported
/// through the existing status channel -- the UI never touches the store, and no data
/// model changes. `AREA` reports area + perimeter of the entity under `at`; `LIST` dumps
/// its type, layer and defining parameters.
struct AreaQueryCommand {
    Vec2 at;
    double pick_radius = 0.0;
};
struct ListQueryCommand {
    Vec2 at;
    double pick_radius = 0.0;
};

/// STRETCH (AutoCAD): move the selection by `delta` under AutoCAD's rule --
///
///   * an object CROSSED by a crossing window has only the vertices and endpoints that
///     lie inside that window moved (partly enclosed => stretched);
///   * an object completely enclosed by the window, or selected individually (a pick or
///     an ordinary window), is MOVED whole.
///
/// The crossing windows are the ones that built the current selection; the engine
/// remembers them as the selection is made (see GeometryEngine::stretch_windows_), so
/// the command only supplies the displacement. That is also what lets objects selected
/// BEFORE the command (noun-verb) stretch correctly: the window that picked them is
/// still on record.
struct StretchSelectionCommand {
    Vec2 delta;
    std::uint64_t group = 0;
};

/// The live STRETCH rubber-band. While `active`, every publish previews the selection
/// stretched by `delta` -- on a scratch store, exactly as a grip drag does, so the real
/// store and the op-log are untouched until the command commits. The preview is built by
/// the SAME function as the commit, so what is shown is what will land.
struct StretchPreviewCommand {
    Vec2 delta;
    bool active = true;
};

/// Copy all selected entities by `delta`, leaving the originals.
struct CopySelectionCommand {
    Vec2 delta;
    std::uint64_t group = 0;
};

/// Mirror the selection across the line a..b; optionally erase the originals.
struct MirrorSelectionCommand {
    Vec2 a;
    Vec2 b;
    bool erase_source = false;
    std::uint64_t group = 0;
};

/// Offset the entity nearest `pick` by `distance` toward `side`.
struct OffsetPickCommand {
    Vec2 pick;
    double radius = 0.0;
    double distance = 0.0;
    Vec2 side;
    std::uint64_t group = 0;
};

/// Trim the (line) entity nearest `pick` to its nearest intersections.
struct TrimPickCommand {
    Vec2 pick;
    double radius = 0.0;
    std::uint64_t group = 0;
};

/// Join the picked lines/arcs/open polylines into a single polyline. `picks[0]` is the
/// source (its layer/props are inherited); the rest are candidates. Endpoints must meet
/// within `radius` (the snap tolerance); candidates that don't connect are skipped (and
/// counted). A chain whose ends meet -> a closed polyline. One undo group.
struct JoinPickCommand {
    std::vector<Vec2> picks;
    double radius = 0.0;
    std::uint64_t group = 0;
};

/// Join every currently-selected line/arc/open polyline that shares endpoints (within
/// `radius`, the snap tolerance) -- each connected chain becomes one polyline; a chain
/// whose ends meet becomes closed. AutoCAD noun-verb JOIN (select objects, then JOIN).
/// One undo group. Entities that connect to nothing else selected are left untouched.
struct JoinSelectionCommand {
    double radius = 0.0;
    std::uint64_t group = 0;
};

/// Rotate the selection by `angle` (radians) about `base`.
struct RotateSelectionCommand {
    Vec2 base;
    double angle = 0.0;
    std::uint64_t group = 0;
    bool copy = false; ///< ROTATE [Copy]: leave the originals, rotate copies
};

/// Scale the selection by `factor` about `base`.
struct ScaleSelectionCommand {
    Vec2 base;
    double factor = 1.0;
    std::uint64_t group = 0;
    bool copy = false; ///< SCALE [Copy]
};

/// Rectangular array of the selection: rows x cols, spaced by (dx, dy).
struct ArrayRectCommand {
    int rows = 1;
    int cols = 1;
    double dx = 0.0;
    double dy = 0.0;
    /// AutoCAD's "Axis angle": rotates the row/column AXES about the base point while
    /// each copy keeps its own orientation. 0 = world-aligned, the usual case.
    double angle = 0.0;
    std::uint64_t group = 0;
};

/// Polar array of the selection: `count` copies around `center` spanning
/// `total_angle` (radians); `rotate_items` rotates each copy to match.
struct ArrayPolarCommand {
    Vec2 center;
    int count = 1;
    double total_angle = 0.0;
    bool rotate_items = true;
    std::uint64_t group = 0;
};

/// Path array of the selection (AutoCAD ARRAYPATH): distribute `count` items along the
/// entity under `pick`, which may be any tessellable curve (line, arc, circle, polyline,
/// spline). The path itself is NOT consumed -- it stays in the drawing, as AutoCAD does.
///
/// `spacing` selects the two AutoCAD methods, mirroring DIVIDE vs MEASURE:
///   0  -- Divide:  `count` items spread over the whole path.
///   >0 -- Measure: items every `spacing` along the path; `count` caps how many (0 = as
///         many as fit).
/// `align` rotates each copy to the path tangent; otherwise copies keep their original
/// orientation and only translate.
struct ArrayPathCommand {
    Vec2 pick;                 ///< picks the path curve
    double pick_radius = 0.0;
    int count = 0;
    double spacing = 0.0;
    bool align = true;
    Vec2 base;                 ///< the point on the selection that rides the path
    bool has_base = false;     ///< false = use the selection's own anchor
    std::uint64_t group = 0;
};

/// Extend the (line) entity nearest `pick` to the nearest boundary edge.
struct ExtendPickCommand {
    Vec2 pick;
    double radius = 0.0;
    std::uint64_t group = 0;
};

/// Fillet two picked lines with a tangent arc of `radius` (0 = clean corner).
struct FilletPickCommand {
    Vec2 pick1;
    Vec2 pick2;
    double radius = 0.0;
    double pick_radius = 0.0;
    std::uint64_t group = 0;
};

/// Chamfer two picked lines, beveling `dist1`/`dist2` from the corner.
struct ChamferPickCommand {
    Vec2 pick1;
    Vec2 pick2;
    double dist1 = 0.0;
    double dist2 = 0.0;
    double pick_radius = 0.0;
    std::uint64_t group = 0;
};

/// Persistence (geometry-thread). `dxf` selects the DXF codec over the native
/// format. Save reads the store to disk; Open/New replace the drawing as ONE
/// store operation (store left unchanged if a load fails).
struct SaveDocumentCommand {
    std::string path;
    bool dxf = false;
};
struct OpenDocumentCommand {
    std::string path;
    bool dxf = false;
    /// Multi-document: true (the UI default) loads into a NEW tab and activates it,
    /// leaving other documents untouched; false replaces the active document in place
    /// (legacy / tests). `name` is the tab display name (empty -> derived from the path).
    bool new_tab = false;
    std::string name;
};
/// Resets the ACTIVE document to an empty drawing in place (internal/test reset). For a
/// new TAB use CreateDocumentCommand.
struct NewDocumentCommand {};

// --- Multi-document (Phase A) ----------------------------------------------
/// Create a new empty untitled document in a new tab and make it active.
struct CreateDocumentCommand {
    std::string name; ///< display name; empty -> the engine assigns "DrawingN"
};
/// Make the document with this id active. The previous active document is parked
/// (its store/undo/selection/dirty preserved in memory); the next snapshot is built
/// from the new active document.
struct SwitchDocumentCommand {
    std::uint64_t id = 0;
};
/// Close the document with this id. Closing the last remaining document resets it to a
/// fresh empty drawing (the app always has one active document). Dirty prompting is the
/// UI's responsibility before this is sent.
struct CloseDocumentCommand {
    std::uint64_t id = 0;
};

// --- Cross-document clipboard (Phase B) ------------------------------------
/// Copy the current selection into the engine's in-process clipboard (snapshots the
/// entities + the source document's layer/dimstyle/block tables, so paste works even
/// after switching or closing the source). Read-only; does not modify any document.
struct CopyClipboardCommand {};
/// Cut = copy the selection to the clipboard, then erase it (one undo group).
struct CutClipboardCommand {
    std::uint64_t group = 0;
};
/// Paste the clipboard into the ACTIVE document at `at` (the clip's reference point lands
/// there), remapping layer/dimstyle/block references by NAME into the active document's
/// tables (creating any that are missing). The pasted entities become the selection. One
/// undo group. Enables cross-document copy/paste + tab-to-tab drag.
struct PasteClipboardCommand {
    Vec2 at;
    std::uint64_t group = 0;
    /// true: place the clip's reference point at `at` (paste-at-cursor, Ctrl+V). false:
    /// keep the entities' original world coordinates (offset 0) -- used by tab-to-tab drag.
    bool at_cursor = true;
};

// --- Annotation (Phase 13): text + dimensions -------------------------------

/// Single-line text. `justify`: 0 left, 1 centre, 2 right. `font` is the font name
/// ("" = the built-in stroke font); resolved to the store's font-table index on apply.
struct AddTextCommand {
    Vec2 pos;
    double height = 2.5;
    double rotation = 0.0;
    std::uint8_t justify = 0;
    std::string content;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    std::string font{}; ///< font name ("" = stroke "Standard")
    /// Text style NAME ("" = Standard). Resolved to the style table on add; a style
    /// with a font supplies the font when `font` is empty.
    std::string style{};
};

/// A dimension defined by `a`/`b` (def points) placed through `line_pt`, drawn
/// with style `style`. `type` matches core::DimType.
struct AddDimensionCommand {
    std::uint8_t type = 0;
    Vec2 a;
    Vec2 b;
    Vec2 line_pt;
    std::uint16_t style = 0;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    DimOverrides overrides = {}; ///< per-dimension style overrides (authoritative)
    DimStyle dim_style = {};      ///< resolved style snapshot for PR display only;
                                  ///< filled by capture_entity, ignored on recreate
    /// Authorable text decoration around the MEASURED value (never replacing it):
    /// raw prefix/suffix (control codes unexpanded) and the tolerance mode + deviations.
    /// Carried here so undo, move, copy, grip-edit and the clipboard preserve it -- they
    /// all round-trip through capture_entity / add_command_to_store.
    std::string prefix = {};
    std::string suffix = {};
    DimTolerance tol = {};
    /// The AutoCAD-style text override (issue #20; raw, `<>` = the measurement) and the
    /// author's displacement of the label from its derived position (issue #21). Both
    /// travel the same capture/recreate path as the decoration above.
    std::string text_override = {};
    Vec2 text_offset = {};
    /// The extra datum of the newer types (see DimData::aux). Zero otherwise.
    double aux = 0.0;
};

/// Object-aware dimensioning: the geometry thread resolves the entity under
/// `pick1` (and `pick2` for angular) via the spatial index + selectable() gate,
/// reads its intrinsic geometry, and creates the matching dimension -- so the user
/// dimensions by SELECTING the object, not by picking raw construction points.
/// `pick2` is the dimension-line placement for Radius/Diameter/Linear/Aligned, or
/// the second-line pick for Angular. The resulting dimension captures DEF POINTS
/// only (no entity reference), so deleting the source entity never dangles it.
/// `type` matches core::DimType.
struct AddObjectDimensionCommand {
    std::uint8_t type = 0;
    Vec2 pick1;
    Vec2 pick2;
    double pick_radius = 0.0;
    std::uint16_t style = 0;
    std::uint64_t group = 0;
    /// DIMJOGGED only: the centre location override and the jog location.
    Vec2 pick3{};
    Vec2 pick4{};
};

/// Non-mutating query: resolve the def points of an object-based dimension under
/// the pick(s) and publish them in the snapshot (`pending_dim_*`) so the UI can
/// rubber-band the full dimension during placement -- WITHOUT creating anything or
/// touching the op-log. Shares the exact resolution path with
/// AddObjectDimensionCommand (no duplicate logic). Issued once when the object is
/// selected; the per-cursor preview is then computed UI-side.
struct ResolveDimObjectCommand {
    std::uint8_t type = 0;
    Vec2 pick1;
    Vec2 pick2;
    double pick_radius = 0.0;
};

/// Direct-manipulation grip editing. One command drives the whole lifecycle:
/// `Begin` arms a drag on grip `grip` of entity `handle`; `Move` updates the live
/// (snapped/ortho-resolved) target `pos`, recomputing a transient preview on a
/// temporary store -- NO store mutation, NO op-log entry; `Commit` applies the edit
/// as exactly one undo `group`; `Cancel` (Esc) drops the drag, entity unchanged.
struct GripDragCommand {
    enum class Phase : std::uint8_t { Begin, Move, Commit, Cancel };
    Phase phase = Phase::Begin;
    EntityHandle handle;     ///< Begin: the entity being edited
    std::uint32_t grip = 0;  ///< Begin: which grip index
    Vec2 pos;                ///< Move/Commit: resolved drag target
    std::uint64_t group = 0; ///< Commit: undo group id
};

/// View scale (world units per pixel) for zoom-adaptive curve tessellation. Sent
/// only when the camera scale actually changes (zoom/resize, never pan). The
/// geometry thread buckets it and re-tessellates curves on a bucket change so
/// arcs/circles stay smooth at any zoom; panning never re-tessellates.
struct SetViewScaleCommand {
    double world_per_px = 1.0;
};

/// Builds a FINE-tolerance snapshot into a dedicated plot buffer (smooth arcs at any
/// paper scale), independent of the live view's tessellation. Read-only -- it never
/// mutates the store. The UI reads it via plot_snapshot() once plot_snapshot_version()
/// bumps. Used by PLOT/print so the output geometry is crisp regardless of zoom.
struct BuildPlotSnapshotCommand {
    double tolerance = 0.01;
};

/// Saves a named PLOT page setup into the document (replacing one of the same name).
/// Marks the document dirty and republishes so the PLOT dialog sees it.
struct AddPageSetupCommand {
    PageSetup setup;
};

/// A quick leader: arrowhead at `tip`, line to `knee`, text label at `knee`.
struct AddLeaderCommand {
    Vec2 tip;
    Vec2 knee;
    double text_height = 2.5;
    std::uint16_t style = 0;
    std::string content;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    std::string font{};          ///< font name ("" = stroke "Standard")
    DimOverrides overrides = {};  ///< per-leader arrow override (ByStyle unless set)
    DimStyle dim_style = {};      ///< resolved style snapshot for PR display only
};

/// Multi-line paragraph text (MTEXT). `block.str_offset/str_len` are ignored;
/// `content` is the raw paragraph string. Layout is computed at render time. `font`
/// is the font name (resolved to block.font, the store's font-table index, on apply).
struct AddMTextCommand {
    MTextBlock block;
    std::string content;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    std::string font{}; ///< font name ("" = stroke "Standard")
};

/// Editable leader (QLEADER): leader `vertices` (vertex 0 = arrow tip), a dimstyle
/// arrow `style`, and an owned paragraph label (`block` + `content`).
struct AddMLeaderCommand {
    std::vector<Vec2> vertices;
    std::uint16_t style = 0;
    MTextBlock block;
    std::string content;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    std::string font{}; ///< label font NAME ("" = stroke "Standard"); resolved to block.font
    DimOverrides overrides = {}; ///< per-leader arrow override (ByStyle unless set)
    DimStyle dim_style = {};      ///< resolved style snapshot for PR display only
};

/// Place a block reference (INSERT) in model space. `block` indexes the block-
/// definition table; the transform is insertion point + X/Y scale + rotation. The
/// referenced geometry is resolved (definition x transform) at snapshot, not copied.
struct AddInsertCommand {
    std::uint16_t block = 0;
    Vec2 pos;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double rotation = 0.0; ///< radians, CCW
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    std::vector<std::string> attribs; ///< attribute values in the block's attdef order
};

/// Create a HATCH from closed boundary loops (loop 0 = outer, the rest islands), a
/// pattern name ("SOLID" = filled), and pattern scale / angle(radians) / origin. The
/// fill or pattern geometry is computed at snapshot time (derived-not-baked).
struct AddHatchCommand {
    std::vector<std::vector<Vec2>> loops;
    std::string pattern_name = "SOLID";
    double pattern_scale = 1.0;
    double pattern_angle = 0.0; ///< radians, CCW
    Vec2 pattern_origin{};
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    Rgb color2{}; ///< GRADIENT: the second colour
};

/// ATTDEF: an attribute definition. `text` gives the placement, props, font and style
/// with the TAG as its content (what model space shows); `prompt` and `def` are what
/// INSERT asks and offers once the definition is inside a block.
struct AddAttDefCommand {
    AddTextCommand text;
    std::string prompt;
    std::string def;
    std::uint8_t flags = 0; ///< kAttInvisible | kAttConstant | kAttVerify | kAttPreset
    std::uint64_t group = 0;
};
/// REFEDIT: open the block reference under `pick` for in-place editing. Its members
/// become ordinary model-space objects (the working set) at the reference's place;
/// REFCLOSE saves them back into the definition or discards the changes.
struct RefEditCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    std::uint64_t group = 0;
};
/// REFSET: add the selection to the working set (they join the block on save) or take
/// it out (they stay ordinary objects).
struct RefSetCommand {
    bool add = true;
    std::uint64_t group = 0;
};
/// REFCLOSE: end the in-place edit. `save` rewrites the block definition from the
/// working set (in the reference's own frame); otherwise the working set is dropped
/// and the reference comes back untouched.
struct RefCloseCommand {
    bool save = true;
    std::uint64_t group = 0;
};
/// A polyline grip's menu (AutoCAD's multi-functional grips): at grip `index` of the
/// polyline `handle`, add a vertex, remove the vertex, or turn the segment into an arc
/// or a line. `pos` is where the new vertex goes for AddVertex (the grip when unset).
struct PolylineVertexCommand {
    enum class Op : std::uint8_t { AddVertex, RemoveVertex, ToArc, ToLine };
    EntityHandle handle;
    std::uint32_t index = 0; ///< a vertex index, or kSegmentGripBase + segment
    Op op = Op::AddVertex;
    std::uint64_t group = 0;
};
/// IMAGEATTACH: place the raster file at `path`. `embed` keeps the encoded bytes in the
/// drawing (up to kMaxEmbeddedImageBytes); otherwise the path is stored relative to the
/// drawing's folder (an image outside it, or an unsaved drawing, is embedded instead).
/// At `scale` 1 one pixel is one drawing unit, as AutoCAD does without resolution data.
struct AttachImageCommand {
    std::string path;
    bool embed = false;
    Vec2 pos{};
    double scale = 1.0;
    double rotation = 0.0; ///< radians, CCW
    std::uint64_t group = 0;
};
/// IMAGECLIP on the image under `pick`: a new rectangular boundary from `a` to `b`
/// (world corners), or Delete (unclipped), ON / OFF (keep the boundary, apply it or not).
struct SetImageClipCommand {
    enum class Mode : std::uint8_t { NewRect, Delete, On, Off };
    Vec2 pick{};
    double pick_radius = 0.0;
    Mode mode = Mode::NewRect;
    Vec2 a{};
    Vec2 b{};
    std::uint64_t group = 0;
};
/// IMAGEFRAME: 0 frames hidden, 1 shown and plotted, 2 shown on screen only.
struct SetImageFrameCommand {
    std::uint8_t mode = 1;
};
/// MODEL / LAYOUT Set / the layout tabs: make model space (0) or a layout (its id) the
/// space that is drawn, picked and edited.
struct SetActiveSpaceCommand {
    std::uint8_t space = 0;  ///< 0 model, a layout id, or 0xFF for "the first layout"
    std::string name;        ///< when set, the layout is found by this name instead
};
/// LAYOUT: New (a fresh sheet named `name`), Copy (a copy of the layout `name` as
/// `new_name`, objects included), Delete (an empty layout), Rename (`name` -> `new_name`).
struct LayoutCommand {
    enum class Op : std::uint8_t { New, Copy, Delete, Rename };
    Op op = Op::New;
    std::string name;
    std::string new_name;
    std::uint64_t group = 0;
};
/// A paper-space viewport (the record form; see ViewportData).
struct AddViewportCommand {
    Vec2 center;
    double width = 100.0;
    double height = 60.0;
    Vec2 view_center;
    double scale = 1.0; ///< paper millimetres per model unit
    bool on = true;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
};
/// MVIEW: a viewport from two sheet corners (or, with `fit_sheet`, filling the sheet
/// inside a 10 mm margin) that shows the whole model, fitted.
struct CreateViewportCommand {
    Vec2 a{};
    Vec2 b{};
    bool fit_sheet = false;
    std::uint64_t group = 0;
};
/// MVIEW ON / OFF / Scale / Center on the viewport under `pick`: fields left at their
/// "keep" values (on = -1, scale = 0, no centre) are unchanged.
struct SetViewportViewCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    int on = -1;
    double scale = 0.0;
    std::optional<Vec2> view_center;
    std::uint64_t group = 0;
};
/// MSPACE: edit model space through the viewport under `pick` (a point inside it, or on
/// its frame). `paper_px_per_mm` is the sheet's on-screen scale at that moment, kept so
/// the model view left on PSPACE maps back into the viewport's scale.
struct EnterMspaceCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    double paper_px_per_mm = 1.0;
};
/// PSPACE from MSPACE: the model view being left (its centre and screen pixels per model
/// unit) becomes the viewport's view, and the sheet comes back.
struct LeaveMspaceCommand {
    Vec2 view_center{};
    double px_per_unit = 0.0;
};
/// XREF Attach: the drawing at `path` becomes a block named after the file (its own
/// blocks come along as "name|block") and one reference is placed.
struct XrefAttachCommand {
    std::string path;
    Vec2 pos{};
    double scale = 1.0;
    double rotation = 0.0;
    std::uint64_t group = 0;
};
/// XREF Reload: re-read `name` ("" = every xref) from its file.
struct XrefReloadCommand {
    std::string name;
};
/// XREF Detach: erase every reference to `name` and drop its definition (and the
/// nested "name|..." definitions nothing else uses).
struct XrefDetachCommand {
    std::string name;
    std::uint64_t group = 0;
};
/// XREF ?: report the attached drawings and where they come from.
struct XrefListCommand {};
/// ATTDISP: 0 Normal (each attribute's own Invisible mode), 1 all ON, 2 all OFF.
struct SetAttDispCommand {
    std::uint8_t mode = 0;
};
/// -ATTEDIT: give the attribute `tag` ("" = every attribute) of the block reference
/// under `pick` a new value.
struct SetInsertAttribCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    std::string tag;
    std::string value;
    std::uint64_t group = 0;
};

/// Create a GD&T feature control frame. `cells` are the ordered cell strings (cell 0 is
/// the characteristic symbol); they are RAW, so `\U+2316` and `%%c` expand at layout
/// time like any other text. `overrides`/`dim_style` mirror AddDimensionCommand exactly:
/// the overrides are authoritative and the style snapshot is for PR display only.
struct AddFcfCommand {
    std::vector<std::string> cells;
    Vec2 pos;
    double rotation = 0.0;
    std::uint16_t style = 0;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    DimOverrides overrides = {};
    DimStyle dim_style = {};
};

/// Create a GD&T datum feature symbol: the boxed `letter`, a leader from `pos` to
/// `tip`, and the filled triangle at the tip.
struct AddDatumCommand {
    std::string letter = "A";
    Vec2 tip;
    Vec2 pos;
    double rotation = 0.0;
    std::uint16_t style = 0;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    DimOverrides overrides = {};
    DimStyle dim_style = {};
};

/// Place a raster image. `def` indexes the image-definition table, which the engine
/// gets-or-adds from `def_source`/`def_bytes` so the UI never has to know the index.
struct AddImageCommand {
    std::uint16_t def = 0;
    Vec2 pos;
    double width = 1.0;
    double height = 1.0;
    double rotation = 0.0;
    bool clipped = false;
    double clip_u0 = 0.0;
    double clip_v0 = 0.0;
    double clip_u1 = 1.0;
    double clip_v1 = 1.0;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
    std::vector<Vec2> clip_polygon; ///< a polygonal clip (image fractions), or empty
};
/// IMAGECLIP New boundary > Polygonal: the boundary as world points on the image.
struct SetImagePolyClipCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    std::vector<Vec2> points;
    std::uint64_t group = 0;
};

/// Create a TABLE. `cells` are the raw cell strings in ROW-MAJOR order (rows*cols of
/// them) with their spans/alignment; `col_widths` and `row_heights` size the grid.
struct AddTableCommand {
    std::uint16_t rows = 0;
    std::uint16_t cols = 0;
    std::vector<TableCell> cells;    ///< str_offset/len are ignored; `texts` carries content
    std::vector<std::string> texts;  ///< parallel to `cells`
    std::vector<double> col_widths;
    std::vector<double> row_heights;
    Vec2 pos;
    double rotation = 0.0;
    std::uint16_t style = 0;
    bool has_title = false;
    bool has_header = false;
    std::uint64_t group = 0;
    std::optional<EntityProps> props = {};
};

/// HATCH "Select objects" mode: the engine reads the current selection, extracts the
/// closed boundary of each selected closed polyline (UI never touches the store), and
/// creates one hatch with the given pattern. Resolved geometry-side, like JOIN.
struct HatchFromSelectionCommand {
    std::string pattern_name = "SOLID";
    double pattern_scale = 1.0;
    double pattern_angle = 0.0; ///< radians, CCW
    std::uint64_t group = 0;
    Rgb color2{}; ///< GRADIENT: the second colour
};

/// HATCH "Pick internal point" mode: the engine traces the closed boundary enclosing
/// `point` from the surrounding geometry (+ islands), then creates the hatch.
struct HatchPickPointCommand {
    Vec2 point;
    std::string pattern_name = "SOLID";
    double pattern_scale = 1.0;
    double pattern_angle = 0.0; ///< radians, CCW
    std::uint64_t group = 0;
    Rgb color2{}; ///< GRADIENT: the second colour
};

/// WIPEOUT [Frames]: show or hide wipeout boundaries (WIPEOUTFRAME).
struct SetWipeoutFramesCommand {
    bool on = true;
};
/// WIPEOUT [Polyline]: a wipeout from the closed polyline under `pick` (arc segments
/// tessellated), optionally erasing the polyline.
struct WipeoutFromPolylineCommand {
    Vec2 pick{};
    double pick_radius = 0.0;
    bool erase = false;
    std::uint64_t group = 0;
};

/// Set one property (universal or type-specific) on every selected entity, as
/// one undo group. The Properties palette's single write path: the descriptor
/// registry (properties_registry.hpp) maps `id` to the field it mutates on each
/// captured Add*Command. Entities the property doesn't apply to are skipped.
struct SetPropertyCommand {
    PropertyId id{};
    PropertyValue value{};
    std::uint64_t group = 0;
};

/// Edit the content of the text-bearing entity (TEXT / MTEXT / QLEADER label)
/// nearest `at`. A content change on the existing entity -- layer/properties/
/// position are preserved -- committed as one undo group. Used by both the
/// double-click editor and the TEXTEDIT/DDEDIT command.
struct EditTextContentCommand {
    Vec2 at;
    double pick_radius = 0.0;
    std::string content;
    std::uint64_t group = 0;
};

/// Toggle lineweight display (AutoCAD LWDISPLAY). Off => thin default everywhere.
struct SetLineweightDisplayCommand {
    bool on = true;
};

/// Set the global linetype scale (AutoCAD LTSCALE). Re-dashes all non-continuous
/// entities at the next snapshot (derived, not stored as geometry). Ignored if <= 0.
struct SetLtscaleCommand {
    double scale = 1.0;
};

/// Add a dimension style (or return the existing index for a known name).
struct AddDimStyleCommand {
    DimStyle style;
};
/// Replace the dimension style at `index` (index 0 stays "Standard").
struct SetDimStyleCommand {
    std::uint16_t index = 0;
    DimStyle style;
};

// --- Layers & properties (geometry-thread) ---------------------------------

/// Add a layer (or ensure one with this name exists).
struct AddLayerCommand {
    Layer layer;
};
/// Replace the properties of the layer at `index` (name/color/linetype/lineweight
/// and the on/frozen/locked flags). Layer 0 cannot be renamed.
struct SetLayerCommand {
    std::uint16_t index = 0;
    Layer layer;
};
/// Remove the layer at `index` (fails for layer 0 / current / non-empty).
struct RemoveLayerCommand {
    std::uint16_t index = 0;
};
/// Make `index` the current layer (new entities land here).
struct SetCurrentLayerCommand {
    std::uint16_t index = 0;
};
/// Move every selected entity to layer `index` (one undo group).
struct SetEntityLayerCommand {
    std::uint16_t index = 0;
    std::uint64_t group = 0;
};
/// Set (or clear) the colour override on every selected entity. `by_layer == true`
/// reverts to ByLayer; otherwise `color` is the explicit override.
struct SetEntityColorCommand {
    bool by_layer = true;
    Rgb color{};
    std::uint64_t group = 0;
};

/// MATCHPROP step 1: capture the entity nearest `point` (within `radius`) as the match
/// source. Snapshots its property values on the geometry thread (UI never reads the
/// store). No undo entry -- it only records the source for subsequent applies.
struct MatchPropPickSourceCommand {
    Vec2 point;
    double radius = 0.0;
};
/// MATCHPROP noun-verb: use the FIRST selected entity as the match source (when MA is run
/// with a selection already active). Reduces the selection to that source. No undo entry.
struct MatchPropSourceFromSelectionCommand {};
/// MATCHPROP step 2: apply the captured source's properties (filtered by `filter`) onto
/// the entity nearest `point`. Each target is its own undo group, so individual matches
/// undo in reverse (AutoCAD behaviour). No-op if there is no captured source / no target.
struct MatchPropApplyCommand {
    Vec2 point;
    double radius = 0.0;
    MatchPropFilter filter{};
    std::uint64_t group = 0;
};

using Command =
    std::variant<AddLineCommand, AddPolylineCommand, AddCircleCommand, AddArcCommand, EraseCommand,
                 ErasePickCommand, UndoLastGroupCommand, RedoLastGroupCommand, UndoLastOpCommand,
                 SetCursorCommand, SelectPickCommand, SelectWindowCommand, SelectAllCommand,
                 ClearSelectionCommand, EraseSelectionCommand, MoveSelectionCommand,
                 CopySelectionCommand, MirrorSelectionCommand, OffsetPickCommand, TrimPickCommand,
                 RotateSelectionCommand, ScaleSelectionCommand, ArrayRectCommand, ArrayPolarCommand,
                 ExtendPickCommand, FilletPickCommand, ChamferPickCommand, SaveDocumentCommand,
                 OpenDocumentCommand, NewDocumentCommand, AddLayerCommand, SetLayerCommand,
                 RemoveLayerCommand, SetCurrentLayerCommand, SetEntityLayerCommand,
                 SetEntityColorCommand, AddTextCommand, AddDimensionCommand, AddDimStyleCommand,
                 SetDimStyleCommand, SetLineweightDisplayCommand, AddLeaderCommand,
                 AddObjectDimensionCommand, ResolveDimObjectCommand, SetViewScaleCommand,
                 GripDragCommand, AddMTextCommand, AddMLeaderCommand, EditTextContentCommand,
                 ArrayPathCommand, AddPointCommand, AddXlineCommand, AddEllipseCommand,
                 AddSplineCommand, SaveNamedViewCommand, DeleteNamedViewCommand,
                 CreateGroupCommand, UngroupCommand, SetPickStyleCommand, SetUnitsCommand,
                 AuditCommand, SetTextStyleCommand, SetCurrentTextStyleCommand, DefineBlockCommand,
                 InsertBlockCommand, WriteBlockCommand, RegenCommand, PeditCommand,
                 SetWipeoutFramesCommand, WipeoutFromPolylineCommand, AddAttDefCommand,
                 SetAttDispCommand, SetInsertAttribCommand, RefEditCommand, RefSetCommand,
                 RefCloseCommand, PolylineVertexCommand, AttachImageCommand, SetImageClipCommand,
                 SetImageFrameCommand, SetActiveSpaceCommand, LayoutCommand, AddViewportCommand,
                 CreateViewportCommand, SetViewportViewCommand, EnterMspaceCommand, LeaveMspaceCommand,
                 XrefAttachCommand, XrefReloadCommand, XrefDetachCommand, XrefListCommand,
                 SetImagePolyClipCommand,
                 DividePathCommand, BreakCommand,
                 AlignSelectionCommand, LengthenCommand, PurgeCommand, StretchPreviewCommand,
                 RevcloudObjectCommand, RevcloudReverseCommand, ExplodeSelectionCommand,
                 SetPropertyCommand, SetLtscaleCommand, AddInsertCommand,
                 BuildPlotSnapshotCommand, AddPageSetupCommand, JoinPickCommand,
                 JoinSelectionCommand, CreateDocumentCommand, SwitchDocumentCommand,
                 CloseDocumentCommand, CopyClipboardCommand, CutClipboardCommand,
                 PasteClipboardCommand, MatchPropPickSourceCommand,
                 MatchPropSourceFromSelectionCommand, MatchPropApplyCommand, AddHatchCommand,
                 HatchFromSelectionCommand, HatchPickPointCommand, AddFcfCommand,
                 AddDatumCommand, AddImageCommand, AddTableCommand,
                 StretchSelectionCommand, AreaQueryCommand, ListQueryCommand,
                 ChainDimensionCommand>;

} // namespace musacad::core
