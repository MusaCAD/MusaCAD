// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "musacad/command/command_context.hpp"
#include "musacad/core/math/math.hpp"

namespace musacad::command {

/// One Dynamic-Input field anchored ON the live rubber-band geometry (the small
/// floating value boxes AutoCAD draws next to the geometry they describe). This is
/// pure data computed from the command's PreviewSpec + the constrained cursor; the
/// UI turns each into a draggable tooltip. No Qt here, so the schema is unit-tested.
struct DynField {
    std::string label;   ///< "Length" / "Width" / "Angle" / "Radius"
    core::Vec2 anchor{};  ///< world-space point on the geometry to float beside
    double value = 0.0;  ///< live numeric value (length / width / angle-deg / radius)
    bool is_angle = false; ///< value is an angle in degrees (UI may suffix a degree sign)
    int slot = 0;        ///< 0 = primary, 1 = secondary; maps to compose/lock below
};

/// The DECLARATIVE per-command field schema: one switch over PreviewKind. A command
/// gains drag-time tooltips by adding a case here -- a row, not new machinery, the
/// same discipline as the properties and grips registries. Returns the positioned
/// fields for the active rubber-band (empty when the command has no dimensional drag).
[[nodiscard]] std::vector<DynField> dyn_fields(const PreviewSpec& pv, core::Vec2 cursor);

/// Compose the coordinate string a typed field submits, EXACTLY as the cursor click
/// would (`@w,h` rectangle, `@len<ang` line, `@rad<ang` circle). A typed value
/// overrides the cursor for that slot; an empty slot follows the live cursor -- so
/// typing one field and leaving the other is "lock this, the other tracks the cursor".
/// Rectangle applies the cursor quadrant's sign to the typed magnitudes. Returns ""
/// when there is nothing to submit (no typed value where one is required). This is the
/// ONE composition shared by the DYN box and the on-geometry field tooltips.
[[nodiscard]] std::string compose_dyn_submit(const PreviewSpec& pv, core::Vec2 cursor,
                                             std::optional<double> primary,
                                             std::optional<double> secondary);

/// The effective "other point" the rubber-band should draw to when one or both DOFs
/// are locked to a typed value -- so the preview visibly reflects a locked dimension
/// while the cursor still drives the unlocked one. Render-side only (no op-log churn).
/// With both slots empty this returns `cursor` unchanged.
[[nodiscard]] core::Vec2 apply_dyn_lock(const PreviewSpec& pv, core::Vec2 cursor,
                                        std::optional<double> primary,
                                        std::optional<double> secondary);

} // namespace musacad::command
