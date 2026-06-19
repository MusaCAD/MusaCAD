// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/properties.hpp"

// The Properties-palette (PR) data model. This header is intentionally light --
// it carries only the *aggregated* view of the current selection (values plus a
// per-field "varies" flag), so it can be embedded in the lock-free RenderSnapshot
// and read by the UI without touching the store. The descriptor registry that
// produces these (and applies edits) lives in properties_registry.hpp, which
// depends on Command.

namespace musacad::core {

/// Stable id for an editable/queryable property. Universal ids apply to every
/// entity; the rest are type-specific (shown only for a homogeneous selection).
enum class PropertyId : std::uint16_t {
    // General (universal -- every entity has these via EntityProps).
    Layer = 0,
    Color,
    Linetype,
    Lineweight,
    // Geometry (read-only display; editable is staged).
    GeomLength,
    GeomStart,
    GeomEnd,
    GeomCenter,
    GeomRadius,
    GeomPos,
    // Text / MTEXT.
    TextContent,
    TextHeight,
    TextRotation, ///< shown/edited in degrees
    TextJustify,  ///< 0=L,1=C,2=R
    TextFont,     ///< read-only for now (single stroke font)
    MtWidthFactor,
    MtLineSpacing,
    MtAttach, ///< 0..8 = TL,TC,TR,ML,MC,MR,BL,BC,BR
    MtWidth,  ///< MTEXT defined wrap width
    // Dimension per-dimension overrides (ByStyle unless overridden). The effective
    // value (override or the dim's DimStyle) is shown; editing sets an override.
    DimArrowType,
    DimArrowSize,
    DimDimColor,
    DimExtColor,
    DimTextHeight,
    DimTextColor,
    DimTextPlacement, ///< Above / Centered
    DimPrecision,
};

/// How the UI renders + edits a field. The UI is generic over these.
enum class PropEditor : std::uint8_t {
    ReadOnly = 0,    ///< value.text shown as a label
    Number,          ///< value.num, free numeric entry
    Text,            ///< value.text, single-line entry
    TextContentEdit, ///< value.text, opens the (multi-line capable) content editor
    LayerCombo,      ///< value.choice = layer index
    ColorOverride,   ///< value.flag = ByLayer; value.color = override
    LinetypeCombo,   ///< value.flag = ByLayer; value.choice = Linetype
    LineweightCombo, ///< value.flag = ByLayer; value.num = hundredths-mm
    JustifyCombo,    ///< value.choice 0..2
    AttachCombo,     ///< value.choice 0..8
    FontCombo,       ///< read-only (single font), value.choice
    // Dimension overrides: value.flag = ByStyle (no override). The combos put
    // "ByStyle" at index 0 (like LinetypeCombo's ByLayer); editing index>0 / a
    // number / a colour sets the override.
    NumberOverride,    ///< value.flag = ByStyle; value.num = effective/override value
    DimArrowTypeCombo, ///< value.choice: 0=ByStyle, 1..4 = ArrowType+1
    DimPlacementCombo, ///< value.choice: 0=ByStyle, 1=Above, 2=Centered
};

/// A typed value carried by a field / a SetPropertyCommand. Only the members
/// relevant to the field's editor are meaningful; equality compares all so the
/// "varies" aggregation is trivial.
struct PropertyValue {
    double num = 0.0;
    std::string text;
    Rgb color{};
    bool flag = false; ///< by-layer for color/linetype/lineweight
    int choice = 0;    ///< enum / index selection

    friend bool operator==(const PropertyValue&, const PropertyValue&) = default;
};

/// The MATCHPROP category a property belongs to -- the registry tags each descriptor
/// with one. Each slot maps 1:1 to a checkbox in the MA Settings dialog. `None` means
/// the property is not copied by MATCHPROP (content, placement, or read-only geometry).
/// Universal slots (Color/Layer/Lineweight/Linetype) copy across any entity kinds;
/// family-scoped slots (Text/Dimension) copy only within a shared EntityFamily.
enum class MatchSlot : std::uint8_t {
    None = 0,
    Color,
    Layer,
    Lineweight,
    Linetype,
    Text,
    Dimension,
};

/// True for the universal MATCHPROP slots (copy regardless of entity family).
[[nodiscard]] constexpr bool match_slot_universal(MatchSlot s) noexcept {
    return s == MatchSlot::Color || s == MatchSlot::Layer || s == MatchSlot::Lineweight ||
           s == MatchSlot::Linetype;
}

/// Which MATCHPROP categories a run copies (the Settings dialog state). All on by
/// default (AutoCAD's default-all-on). `ltscale`/`plotstyle`/`hatch`/`polyline` are
/// surfaced for AutoCAD parity but gate no registry descriptors yet (LTSCALE is global,
/// plot style/hatch unmodelled, polyline has no type-specific registry property).
struct MatchPropFilter {
    bool color = true;
    bool layer = true;
    bool lineweight = true;
    bool linetype = true;
    bool ltscale = true;   ///< reserved (LTSCALE is a global, not a per-entity property)
    bool plotstyle = true; ///< reserved (plot style not modelled)
    bool text = true;
    bool dimension = true;
    bool hatch = true;     ///< reserved (hatch not implemented)
    bool polyline = true;  ///< reserved (no polyline-specific registry descriptor yet)

    /// Whether the category gating `slot` is currently enabled.
    [[nodiscard]] constexpr bool allows(MatchSlot s) const noexcept {
        switch (s) {
        case MatchSlot::Color:
            return color;
        case MatchSlot::Layer:
            return layer;
        case MatchSlot::Lineweight:
            return lineweight;
        case MatchSlot::Linetype:
            return linetype;
        case MatchSlot::Text:
            return text;
        case MatchSlot::Dimension:
            return dimension;
        case MatchSlot::None:
            return false;
        }
        return false;
    }

    friend bool operator==(const MatchPropFilter&, const MatchPropFilter&) = default;
};

/// One aggregated row for the panel.
struct PropertyField {
    PropertyId id{};
    std::string group; ///< "General" / "Geometry" / "Text"
    std::string label;
    PropEditor editor = PropEditor::ReadOnly;
    bool varies = false; ///< values differ across the selection
    PropertyValue value{};

    friend bool operator==(const PropertyField&, const PropertyField&) = default;
};

/// The whole palette view of the current selection.
struct SelectionSummary {
    int count = 0;
    bool mixed = false;          ///< selection spans more than one entity kind
    std::uint8_t kind_plus1 = 0; ///< EntityKind+1 when homogeneous, else 0
    std::string type_label;      ///< e.g. "Line", "3 Texts", "Mixed (5)"
    std::vector<PropertyField> fields;

    friend bool operator==(const SelectionSummary&, const SelectionSummary&) = default;
};

} // namespace musacad::core
