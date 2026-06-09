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
