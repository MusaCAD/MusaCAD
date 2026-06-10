#pragma once

#include <cstdint>
#include <type_traits>

namespace musacad::core {

/// The kind of geometric primitive an EntityHandle refers to. Determines which
/// SoA arena in the GeometryStore owns the entity.
enum class EntityKind : std::uint16_t {
    Point,
    Line,
    Polyline,
    Circle,
    Arc,
    Spline,
    Text,
    Dimension,
    Leader,
    MText,
    MLeader,
    Insert,
};

/// A generational handle to an entity in the GeometryStore.
///
/// `index` selects a slot; `generation` detects stale handles: when a slot is
/// freed and later reused its generation is bumped, so an old handle compares
/// unequal to the live generation and is reported invalid. Trivially copyable
/// and self-contained, so it is safe to copy across threads.
struct EntityHandle {
    static constexpr std::uint32_t kInvalidIndex = 0xFFFF'FFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;
    EntityKind kind = EntityKind::Point;

    [[nodiscard]] constexpr bool is_null() const noexcept { return index == kInvalidIndex; }

    [[nodiscard]] static constexpr EntityHandle null() noexcept { return EntityHandle{}; }

    friend constexpr bool operator==(const EntityHandle&, const EntityHandle&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<EntityHandle>,
              "EntityHandle must be trivially copyable to pass across threads");

} // namespace musacad::core
