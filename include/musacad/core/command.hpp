#pragma once

#include <cstdint>
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
};

/// Erases the entity nearest `world` within `pick_radius` (cursor-pick, via the
/// shared spatial index). Undoable.
struct ErasePickCommand {
    Vec2 world;
    double pick_radius = 0.0;
    std::uint64_t group = 0;
};

using Command =
    std::variant<AddLineCommand, AddPolylineCommand, AddCircleCommand, AddArcCommand, EraseCommand,
                 ErasePickCommand, UndoLastGroupCommand, RedoLastGroupCommand, UndoLastOpCommand,
                 SetCursorCommand>;

} // namespace musacad::core
