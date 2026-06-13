// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstdint>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// Object-snap categories. The numeric value is the precedence (lower wins when
/// several candidates fall within the aperture), in AutoCAD's rough order.
/// `Centroid` is a Musa extension (no AutoCAD equivalent).
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
    Centroid = 9, // Musa extension
    Nearest = 10,
};

/// Bit flags for the enabled snap categories (a mask of 1 << SnapType).
inline constexpr std::uint32_t snap_bit(SnapType t) noexcept {
    return 1u << static_cast<std::uint32_t>(t);
}
inline constexpr std::uint32_t kAllSnaps =
    snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint) | snap_bit(SnapType::Center) |
    snap_bit(SnapType::Node) | snap_bit(SnapType::Quadrant) | snap_bit(SnapType::Intersection) |
    snap_bit(SnapType::Perpendicular) | snap_bit(SnapType::Tangent) |
    snap_bit(SnapType::Centroid) | snap_bit(SnapType::Nearest);

struct SnapResult {
    bool found = false;
    SnapType type = SnapType::None;
    Vec2 point{};
};

} // namespace musacad::core
