// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>

namespace musacad::core {

/// An RGB colour (0-255 per channel). Used for both layer colours (always
/// explicit) and entity colour overrides.
struct Rgb {
    std::uint8_t r = 255;
    std::uint8_t g = 255;
    std::uint8_t b = 255;
    friend bool operator==(const Rgb&, const Rgb&) = default;
};

/// The standard linetypes Musa CAD models. Stored as the property; visual
/// stippling is applied render-side.
enum class Linetype : std::uint8_t {
    Continuous = 0,
    Dashed = 1,
    Center = 2,
    Hidden = 3,
};

/// A drawing layer: name + default properties + display state. Lineweight is in
/// hundredths of a millimetre (the DXF unit, code 370; 0..211).
struct Layer {
    std::string name;
    Rgb color{255, 255, 255};
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25; ///< 0.25 mm
    bool on = true;               ///< off layers don't render
    bool frozen = false;          ///< frozen layers don't render
    bool locked = false;          ///< locked layers render but can't be selected/modified
    friend bool operator==(const Layer&, const Layer&) = default;
};

/// Dimension subtype. Linear/Aligned are fully implemented; the rest are staged.
enum class DimType : std::uint8_t {
    Linear = 0,  ///< horizontal/vertical (dominant axis of the def points)
    Aligned = 1, ///< parallel to the measured segment
    Radius = 2,
    Diameter = 3,
    Angular = 4,
};

/// Arrowhead style for dimensions and leaders.
enum class ArrowType : std::uint8_t {
    Filled = 0, ///< solid filled triangle (closed)
    Tick = 1,   ///< architectural 45-degree tick
    Open = 2,   ///< open (two barb lines)
    Dot = 3,    ///< filled dot
};

/// A per-element colour: inherited from the entity's layer, or an explicit RGB.
struct ElementColor {
    bool by_layer = true;
    Rgb color{};
    friend bool operator==(const ElementColor&, const ElementColor&) = default;
    [[nodiscard]] Rgb resolve(Rgb base) const noexcept { return by_layer ? base : color; }
};

/// A dimension style: the formatting all dimensions resolve through. "Standard"
/// always exists at index 0 of the store's dimstyle table.
struct DimStyle {
    std::string name;
    double text_height = 2.5;
    double arrow_size = 2.5;
    std::uint8_t arrow_type = 0; ///< ArrowType
    double ext_offset = 0.6;     ///< gap from def point to the extension line start
    double ext_extension = 1.25; ///< how far the extension line passes the dim line
    std::uint8_t precision = 2;  ///< decimal places in the measured text
    bool text_above = true;      ///< text sits above the dim line (else centred)
    std::uint8_t dim_lineweight = 25; ///< dimension/extension line weight (hundredths mm)

    // Per-element colours (ByLayer by default, AutoCAD-style).
    ElementColor dim_color{};
    ElementColor ext_color{};
    ElementColor text_color{};
    ElementColor arrow_color{};

    friend bool operator==(const DimStyle&, const DimStyle&) = default;
};

/// Per-dimension property overrides (AutoCAD's PR "this dimension only"). Mirrors
/// the ByLayer/override pattern: a bit in `mask` means "this field is overridden;
/// use the value here instead of the dimension's DimStyle". No bit => ByStyle. The
/// effective value is resolved once, in compute_dim_geometry (override-first). Kept
/// compact and only in the dimension's arena -- hot structs are untouched.
struct DimOverrides {
    enum Bit : std::uint16_t {
        kTextHeight = 1u << 0,
        kArrowSize = 1u << 1,
        kArrowType = 1u << 2,
        kPrecision = 1u << 3,
        kTextAbove = 1u << 4,
        kDimColor = 1u << 5,
        kExtColor = 1u << 6,
        kTextColor = 1u << 7,
    };
    std::uint16_t mask = 0;
    std::uint8_t arrow_type = 0;
    std::uint8_t precision = 2;
    bool text_above = true;
    double text_height = 2.5;
    double arrow_size = 2.5;
    Rgb dim_color{};
    Rgb ext_color{};
    Rgb text_color{};

    [[nodiscard]] bool has(Bit b) const noexcept { return (mask & b) != 0; }
    void set(Bit b, bool on) noexcept { mask = on ? (mask | b) : (mask & ~b); }

    friend bool operator==(const DimOverrides&, const DimOverrides&) = default;
};

/// Apply a dimension's overrides onto a copy of its style, producing the effective
/// style. The ONE place override-vs-style is decided (called from compute_dim_geometry).
[[nodiscard]] inline DimStyle apply_dim_overrides(DimStyle s, const DimOverrides& o) noexcept {
    if (o.has(DimOverrides::kTextHeight)) {
        s.text_height = o.text_height;
    }
    if (o.has(DimOverrides::kArrowSize)) {
        s.arrow_size = o.arrow_size;
    }
    if (o.has(DimOverrides::kArrowType)) {
        s.arrow_type = o.arrow_type;
    }
    if (o.has(DimOverrides::kPrecision)) {
        s.precision = o.precision;
    }
    if (o.has(DimOverrides::kTextAbove)) {
        s.text_above = o.text_above;
    }
    if (o.has(DimOverrides::kDimColor)) {
        s.dim_color = ElementColor{false, o.dim_color};
    }
    if (o.has(DimOverrides::kExtColor)) {
        s.ext_color = ElementColor{false, o.ext_color};
    }
    if (o.has(DimOverrides::kTextColor)) {
        s.text_color = ElementColor{false, o.text_color};
    }
    return s;
}

/// Per-entity property attributes. Each property is either inherited from the
/// entity's layer (its ByLayer flag set) or an explicit override. Kept compact
/// (8 bytes) because every entity carries one: a packed flags byte instead of
/// three bools, and an 8-bit lineweight instead of a double. Remains an aggregate
/// (brace-init {layer, color, linetype, lineweight, flags}).
struct EntityProps {
    static constexpr std::uint8_t kColorByLayer = 1u << 0;
    static constexpr std::uint8_t kLinetypeByLayer = 1u << 1;
    static constexpr std::uint8_t kLineweightByLayer = 1u << 2;
    static constexpr std::uint8_t kAllByLayer =
        kColorByLayer | kLinetypeByLayer | kLineweightByLayer;

    std::uint16_t layer = 0; ///< index into the store's layer table
    Rgb color{};             ///< override colour when !color_by_layer()
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25;       ///< override, hundredths of a mm
    std::uint8_t flags = kAllByLayer;   ///< which properties are ByLayer

    [[nodiscard]] bool color_by_layer() const noexcept { return (flags & kColorByLayer) != 0; }
    [[nodiscard]] bool linetype_by_layer() const noexcept {
        return (flags & kLinetypeByLayer) != 0;
    }
    [[nodiscard]] bool lineweight_by_layer() const noexcept {
        return (flags & kLineweightByLayer) != 0;
    }
    void set_color_by_layer(bool v) noexcept { set_flag(kColorByLayer, v); }
    void set_linetype_by_layer(bool v) noexcept { set_flag(kLinetypeByLayer, v); }
    void set_lineweight_by_layer(bool v) noexcept { set_flag(kLineweightByLayer, v); }

    friend bool operator==(const EntityProps&, const EntityProps&) = default;

private:
    void set_flag(std::uint8_t bit, bool v) noexcept {
        if (v) {
            flags = static_cast<std::uint8_t>(flags | bit);
        } else {
            flags = static_cast<std::uint8_t>(flags & ~bit);
        }
    }
};

/// The effective properties used to draw an entity, after ByLayer resolution.
struct ResolvedProps {
    Rgb color;
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25; ///< hundredths of a mm
};

/// Resolves an entity's effective properties: an explicit override wins,
/// otherwise the value is inherited from its layer (ByLayer). This is the
/// conceptual core of the property model.
[[nodiscard]] inline ResolvedProps resolve(const EntityProps& e, const Layer& layer) noexcept {
    ResolvedProps out;
    out.color = e.color_by_layer() ? layer.color : e.color;
    out.linetype = e.linetype_by_layer() ? layer.linetype : e.linetype;
    out.lineweight = e.lineweight_by_layer() ? layer.lineweight : e.lineweight;
    return out;
}

} // namespace musacad::core
