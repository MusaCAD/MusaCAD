#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

// Commands are the ONLY thing that crosses the UI -> geometry boundary (via the
// MPSC queue). The geometry thread applies them to the store and maintains the
// undo log. Entity-creating commands carry a `group` id: all messages emitted by
// one command-line invocation share a group, so a single command-line undo
// removes the whole invocation. `group` defaults to 0 for ad-hoc submitters.

/// Which entities ERASE targets (selection/picking arrives in Phase 5).
enum class EraseScope : std::uint8_t { Last, All };

struct AddLineCommand {
    Vec2 a;
    Vec2 b;
    std::uint64_t group = 0;
};

struct AddPolylineCommand {
    std::vector<Vec2> points;
    bool closed = false;
    std::uint64_t group = 0;
};

struct AddCircleCommand {
    Vec2 center;
    double radius = 0.0;
    std::uint64_t group = 0;
};

struct AddArcCommand {
    Vec2 center;
    double radius = 0.0;
    double start_angle = 0.0; ///< radians, CCW
    double end_angle = 0.0;
    std::uint64_t group = 0;
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
};
struct NewDocumentCommand {};

using Command =
    std::variant<AddLineCommand, AddPolylineCommand, AddCircleCommand, AddArcCommand, EraseCommand,
                 ErasePickCommand, UndoLastGroupCommand, RedoLastGroupCommand, UndoLastOpCommand,
                 SetCursorCommand, SelectPickCommand, SelectWindowCommand, SelectAllCommand,
                 ClearSelectionCommand, EraseSelectionCommand, MoveSelectionCommand,
                 CopySelectionCommand, MirrorSelectionCommand, OffsetPickCommand, TrimPickCommand,
                 RotateSelectionCommand, ScaleSelectionCommand, ArrayRectCommand, ArrayPolarCommand,
                 ExtendPickCommand, FilletPickCommand, ChamferPickCommand, SaveDocumentCommand,
                 OpenDocumentCommand, NewDocumentCommand>;

} // namespace musacad::core
