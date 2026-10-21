// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

/// True when the drag is a length + angle from an anchor: LINE's rubber band, or PLINE's
/// next-point pick (measured from the last vertex).
[[nodiscard]] bool dyn_segment_like(const PreviewSpec& pv) noexcept;

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

/// DYNDIGRIP: the fields a hot grip's stretch shows -- 1 the resulting length (a circle's
/// radius), 2 the length change, 4 the absolute angle, 8 the angle change, 16 an arc's radius
/// (31, all). DYNDIVIS: how many at a time -- 0 the length change only, 1 it and the resulting
/// length (the default), 2 all DYNDIGRIP asks for. Kept for the session, and by the UI in its
/// settings.
[[nodiscard]] int& dyndigrip() noexcept;
[[nodiscard]] int& dyndivis() noexcept;

/// What a hot grip's stretch is measured from: a line's other end, the polyline vertex before
/// (after, for an open polyline's first), the centre for a circle's or an arc's radius grip
/// (`radius` set). Nullopt for the other grips. `grips`: the selection's grips.
[[nodiscard]] std::optional<core::Vec2> grip_dyn_anchor(std::span<const core::GripInfo> grips,
                                                        core::EntityHandle h, std::uint32_t index,
                                                        bool& radius);

/// A hot grip's stretch measured from the object (PreviewSpec::grip_anchor; dyn_fields numbers
/// its fields' slots in the order shown).
[[nodiscard]] bool dyn_grip_measured(const PreviewSpec& pv) noexcept;
/// Where the grip goes with the values typed in its fields (by slot; nullopt follows the
/// cursor): a length or a length change keeps the cursor's direction, an angle the length.
[[nodiscard]] core::Vec2 grip_dyn_point(const PreviewSpec& pv, core::Vec2 cursor,
                                        std::span<const std::optional<double>> typed);
/// The absolute point (`#x,y`) that puts it there; "" with nothing typed.
[[nodiscard]] std::string compose_grip_submit(const PreviewSpec& pv, core::Vec2 cursor,
                                              std::span<const std::optional<double>> typed);

/// DYNPICOORDS: Dynamic Input's pointer input takes a second or next point as relative (0,
/// AutoCAD's default) or absolute (1) coordinates. Kept for the session, and by the UI in its
/// settings, as AutoCAD keeps it in the registry.
[[nodiscard]] int& dynpicoords() noexcept;

/// A coordinate typed into Dynamic Input, as AutoCAD's pointer input takes it: for a second or
/// next point (`next`: a rubber band from the point before) it is relative to that point -- `@`
/// put in front -- unless DYNPICOORDS is 1; `#` in front makes it absolute (the `#` taken off)
/// and `@` relative, either way. Nullopt for what is not a coordinate (`x,y` or `dist<angle`).
[[nodiscard]] std::optional<std::string> dyn_pointer_input(std::string_view typed, bool next);

/// A prompt asking for a point (`Specify first point:`, `Specify center point for circle`,
/// `Specify insertion point`, `Specify first corner point`): Dynamic Input's pointer input
/// shows the cursor's coordinates there.
[[nodiscard]] bool prompt_asks_point(std::string_view prompt);

} // namespace musacad::command
