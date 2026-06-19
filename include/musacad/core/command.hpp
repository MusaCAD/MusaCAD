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
};

/// Box select. `crossing` false = window (entities fully enclosed), true =
/// crossing (entities touched/crossed).
struct SelectWindowCommand {
    Vec2 min;
    Vec2 max;
    bool crossing = false;
    bool additive = false;
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
};

/// Scale the selection by `factor` about `base`.
struct ScaleSelectionCommand {
    Vec2 base;
    double factor = 1.0;
    std::uint64_t group = 0;
};

/// Rectangular array of the selection: rows x cols, spaced by (dx, dy).
struct ArrayRectCommand {
    int rows = 1;
    int cols = 1;
    double dx = 0.0;
    double dy = 0.0;
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
    std::string font{}; ///< font name ("" = stroke "Standard")
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
                 SetPropertyCommand, SetLtscaleCommand, AddInsertCommand,
                 BuildPlotSnapshotCommand, AddPageSetupCommand, JoinPickCommand,
                 JoinSelectionCommand, CreateDocumentCommand, SwitchDocumentCommand,
                 CloseDocumentCommand, CopyClipboardCommand, CutClipboardCommand,
                 PasteClipboardCommand, MatchPropPickSourceCommand,
                 MatchPropSourceFromSelectionCommand, MatchPropApplyCommand>;

} // namespace musacad::core
