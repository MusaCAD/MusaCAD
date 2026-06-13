// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstdint>
#include <vector>

#include "musacad/core/command.hpp"
#include "musacad/core/entity_handle.hpp"
#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// The role of a grip, mostly informational (display + intent). The concrete edit
/// is driven by (entity kind, grip index) inside edit_for_grip_drag.
enum class GripKind : std::uint8_t {
    Move,     ///< drags the whole entity (line midpoint, circle/arc/text/dim move)
    Endpoint, ///< drags one end of a line/arc
    Radius,   ///< drags a circle/arc radius (quadrant / mid)
    Vertex,   ///< drags one polyline vertex
    DimDef,   ///< drags a dimension definition point (re-measures)
    DimLine,  ///< drags the dimension line offset (value unchanged)
};

/// A single grip handle: a world position, its role, and the per-entity index that
/// identifies it to edit_for_grip_drag.
struct Grip {
    Vec2 pos;
    GripKind kind = GripKind::Move;
    std::uint32_t index = 0;
};

/// The entity as an Add* command (parametric; carries its exact props). One home
/// for the capture used by undo, move, and grip editing.
[[nodiscard]] Command capture_entity(const GeometryStore& store, EntityHandle h);

/// Applies an Add* command to `store`. Uses the command's props if set, else
/// `fallback` (the current-layer ByLayer props). Shared by the engine's create
/// path and the grip-drag preview (which builds onto a temporary store).
EntityHandle add_command_to_store(GeometryStore& store, const Command& cmd, EntityProps fallback);

/// Appends the grips of entity `h` (display + hit-test). Empty for kinds without
/// grips. Caller gates on selectable()/visibility.
void grips_of(const GeometryStore& store, EntityHandle h, std::vector<Grip>& out);

/// The entity edited by dragging grip `grip_index` to `newpos`, as an Add* command
/// (parametric, props preserved). If the grip isn't editable, returns the entity
/// unchanged (its capture). Never bakes tessellation; dimensions stay def-points.
[[nodiscard]] Command edit_for_grip_drag(const GeometryStore& store, EntityHandle h,
                                         std::uint32_t grip_index, Vec2 newpos);

} // namespace musacad::core
