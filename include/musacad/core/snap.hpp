#pragma once

#include <cstdint>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// Object-snap categories, in priority order (lower value wins ties).
enum class SnapType : std::uint8_t {
    None = 0,
    Endpoint = 1,
    Midpoint = 2,
    Center = 3,
    Intersection = 4,
    Nearest = 5,
};

/// Bit flags for the enabled snap categories (a mask of 1 << SnapType).
inline constexpr std::uint32_t snap_bit(SnapType t) noexcept {
    return 1u << static_cast<std::uint32_t>(t);
}
inline constexpr std::uint32_t kAllSnaps =
    snap_bit(SnapType::Endpoint) | snap_bit(SnapType::Midpoint) | snap_bit(SnapType::Center) |
    snap_bit(SnapType::Intersection) | snap_bit(SnapType::Nearest);

struct SnapResult {
    bool found = false;
    SnapType type = SnapType::None;
    Vec2 point{};
};

} // namespace musacad::core
