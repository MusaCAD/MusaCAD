// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <vector>

#include "musacad/core/command.hpp"
#include "musacad/core/entity_handle.hpp"
#include "musacad/core/properties_palette.hpp"

// The Properties-palette descriptor registry: the single place that knows, per
// property, which group/label/editor it has and how to read (aggregate) and
// write it on a captured Add*Command. Adding a type's deep group later means
// registering descriptors here -- the engine apply path and the UI renderer stay
// generic (no per-type switch in either).

namespace musacad::core {

/// The EntityKind a captured Add*Command represents.
[[nodiscard]] EntityKind kind_of(const Command& c) noexcept;

/// Aggregate the captured commands of the current selection into the palette
/// view: which fields apply (universal for mixed selections; type fields when
/// homogeneous), each value, and a "varies" flag where the entities disagree.
[[nodiscard]] SelectionSummary summarize_selection(const std::vector<Command>& captured);

/// True if `id` applies to entities of `kind` (universal ids always do).
[[nodiscard]] bool property_applies(PropertyId id, EntityKind kind) noexcept;

/// Apply `value` for property `id` into the captured command in place (no-op if
/// the property does not apply to that command's kind). Used by the engine's
/// SetPropertyCommand apply, per selected entity.
void write_property(Command& c, PropertyId id, const PropertyValue& value);

} // namespace musacad::core
