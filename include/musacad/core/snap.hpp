// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// Object-snap categories. The numeric value is the bit of the running-snap mask and,
/// but for Extension, the precedence too (see snap_rank): lower wins when several
/// candidates fall within the aperture, in AutoCAD's rough order.
enum class SnapType : std::uint8_t {
    None = 0,
    Endpoint = 1,
    Midpoint = 2,
    Center = 3,
    Node = 4,
    Quadrant = 5,
    Intersection = 6,
    Perpendicular = 7,
    Tangent = 8,
    GeometricCenter = 9,       ///< the centroid of a closed polyline (AutoCAD's GCEN)
    Insertion = 10,            ///< block / text insertion point
    ApparentIntersection = 11, ///< where extended lines/arcs would cross
    Parallel = 12,             ///< a point parallel to a line through the from-point
    Nearest = 13,              ///< last: it must never out-rank a geometric snap
    Extension = 14,            ///< along the extension of a line or arc whose end was acquired
};

/// The precedence of a category within the aperture (lower wins). Extension lies on no
/// object, so every snap to one out-ranks it; Nearest stays last.
inline constexpr std::uint8_t snap_rank(SnapType t) noexcept {
    if (t == SnapType::Extension) {
        return 13;
    }
    if (t == SnapType::Nearest) {
        return 14;
    }
    return static_cast<std::uint8_t>(t);
}

/// Bit flags for the enabled snap categories (a mask of 1 << SnapType).
inline constexpr std::uint32_t snap_bit(SnapType t) noexcept {
    return 1u << static_cast<std::uint32_t>(t);
}
inline constexpr std::uint32_t kAllSnaps =
    snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint) | snap_bit(SnapType::Center) |
    snap_bit(SnapType::Node) | snap_bit(SnapType::Quadrant) | snap_bit(SnapType::Intersection) |
    snap_bit(SnapType::Perpendicular) | snap_bit(SnapType::Tangent) |
    snap_bit(SnapType::GeometricCenter) | snap_bit(SnapType::Insertion) |
    snap_bit(SnapType::Nearest) | snap_bit(SnapType::Extension);
// Apparent intersection and Parallel are opt-in (as in AutoCAD's defaults): the first
// snaps to points that lie on no object, the second needs a previous point.

/// The running snaps a fresh session starts with: AutoCAD's default OSMODE (4133) --
/// Endpoint, Center, Intersection and Extension.
inline constexpr std::uint32_t kDefaultRunningSnaps =
    snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Center) |
    snap_bit(SnapType::Intersection) | snap_bit(SnapType::Extension);

struct SnapResult {
    bool found = false;
    SnapType type = SnapType::None;
    Vec2 point{};
    /// Extension: the acquired end the point was extended from (the dashed path drawn
    /// to the point starts there).
    bool has_path = false;
    Vec2 path_from{};
};

} // namespace musacad::core
