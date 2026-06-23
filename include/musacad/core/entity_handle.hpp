// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

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
    Hatch,
};

/// Coarse classification of an EntityKind, used by MATCHPROP to decide when
/// type-specific (family-scoped) properties may travel from a source to a target.
/// Universal properties (colour/layer/lineweight/linetype) ignore this; family-scoped
/// ones (text, dimension, …) only copy when source and target share a family.
enum class EntityFamily : std::uint8_t {
    SimpleGeometry, ///< Point, Line, Circle, Arc, Spline (no type-specific properties)
    Text,           ///< Text, MText, Leader, MLeader (font/height/justify/…)
    Dimension,      ///< Dimension (dimstyle + per-dim overrides)
    Polyline,       ///< Polyline
    Insert,         ///< block reference (no type-specific properties matched)
    Hatch,          ///< reserved (not yet implemented)
};

/// The family an EntityKind belongs to. The single classification table MATCHPROP
/// reads; it never defines its own.
[[nodiscard]] constexpr EntityFamily family_of(EntityKind k) noexcept {
    switch (k) {
    case EntityKind::Text:
    case EntityKind::MText:
    case EntityKind::Leader:
    case EntityKind::MLeader:
        return EntityFamily::Text;
    case EntityKind::Dimension:
        return EntityFamily::Dimension;
    case EntityKind::Polyline:
        return EntityFamily::Polyline;
    case EntityKind::Insert:
        return EntityFamily::Insert;
    case EntityKind::Hatch:
        return EntityFamily::Hatch;
    case EntityKind::Point:
    case EntityKind::Line:
    case EntityKind::Circle:
    case EntityKind::Arc:
    case EntityKind::Spline:
        return EntityFamily::SimpleGeometry;
    }
    return EntityFamily::SimpleGeometry;
}

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
