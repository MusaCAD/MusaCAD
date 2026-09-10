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
    Fcf,   ///< GD&T feature control frame
    Datum, ///< GD&T datum feature symbol
    Image, ///< placed raster image
    Table, ///< a grid of text cells (BOM, revision block, parts list)
    Xline, ///< construction line: infinite (XLINE) or semi-infinite (RAY)
    Ellipse, ///< ellipse or elliptical arc (centre, major axis, ratio, param range)
    AttDef,  ///< attribute definition (ATTDEF): text-like, shows its tag; becomes a block attribute
    Viewport, ///< paper-space window onto model space (MVIEW)
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
    case EntityKind::AttDef:
    case EntityKind::MText:
    case EntityKind::Leader:
    case EntityKind::MLeader:
        return EntityFamily::Text;
    // GD&T shares DIMSTYLE + DimOverrides with dimensions, so it is deliberately in the
    // DIMENSION family rather than a new one: that is what lets MATCHPROP carry text
    // height and the element colours from a dimension onto a feature control frame,
    // which is precisely the "GD&T annotation matches the drawing's dimensions
    // automatically" the issue asks for. A separate GdtFamily would have blocked it.
    // (Same reasoning that kept Leader/MLeader in the Text family rather than forking.)
    case EntityKind::Dimension:
    case EntityKind::Fcf:
    case EntityKind::Datum:
        return EntityFamily::Dimension;
    case EntityKind::Polyline:
        return EntityFamily::Polyline;
    case EntityKind::Insert:
        return EntityFamily::Insert;
    case EntityKind::Hatch:
        return EntityFamily::Hatch;
    // A placed image has no type-specific properties worth MATCHPROP-ing (its size and
    // clip describe THIS placement), so it sits with the other reference-like kinds --
    // the same call INSERT made -- and only the universal properties travel.
    case EntityKind::Image:
    case EntityKind::Viewport:
    // A table's type-specific state is its CONTENT (cells, sizes), which MATCHPROP must
    // never copy -- the same reasoning that leaves TextContent unmatched. So it sits with
    // the reference-like kinds and only universal properties travel.
    case EntityKind::Table:
        return EntityFamily::Insert;
    case EntityKind::Point:
    case EntityKind::Line:
    case EntityKind::Circle:
    case EntityKind::Arc:
    case EntityKind::Spline:
    case EntityKind::Xline:
    case EntityKind::Ellipse:
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
