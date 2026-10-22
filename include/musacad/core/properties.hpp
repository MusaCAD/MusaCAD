// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/math/vec2.hpp"

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

/// A linetype loaded into the drawing (-LINETYPE Load, the Linetype Manager, an AutoCAD
/// drawing's own): its name, description and pattern as a .lin file writes it -- a dash
/// positive, a gap negative, 0 a dot -- in drawing units. It is the Linetype value
/// kFirstLoadedLinetype plus its place in the drawing's linetype table.
struct LinetypeDef {
    std::string name;
    std::string description{};
    std::vector<double> pattern{};
    friend bool operator==(const LinetypeDef&, const LinetypeDef&) = default;
};
inline constexpr std::uint8_t kFirstLoadedLinetype = 4;
/// The most a drawing loads (the Linetype values up to 255).
inline constexpr std::size_t kMaxLoadedLinetypes = 252;

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
    bool vp_freeze_new = false;   ///< frozen in every viewport created later (VPLAYER Vpvisdflt)
    bool plot = true;             ///< plotted (the manager's Plot column; DXF 290)
    std::string description{};    ///< the manager's Description column
    std::uint8_t transparency = 0; ///< 0..90 percent (the manager's Transparency column, -LAYER TRansparency)
    friend bool operator==(const Layer&, const Layer&) = default;
};

/// A named layer state (LAYERSTATE): every layer's settings when it was saved, by name,
/// and the layer that was current.
struct LayerState {
    std::string name;
    std::string current;
    std::vector<Layer> layers;
    friend bool operator==(const LayerState&, const LayerState&) = default;
};

/// A layer filter of the drawing's own (the Layer Properties Manager): a group filter, a named
/// set of layers by name; or, with `property`, a property filter -- the layers whose name matches
/// `pattern` (* and ? wild cards; empty: any) and whose on, frozen, locked and plot settings
/// are as asked (0 any, 1 yes, 2 no).
struct LayerGroupFilter {
    std::string name;
    std::vector<std::string> layers;
    bool property = false;
    std::string pattern{};
    std::uint8_t on = 0;
    std::uint8_t frozen = 0;
    std::uint8_t locked = 0;
    std::uint8_t plot = 0;
    friend bool operator==(const LayerGroupFilter&, const LayerGroupFilter&) = default;
};

/// Dimension subtype. Linear/Aligned are fully implemented; the rest are staged.
enum class DimType : std::uint8_t {
    Linear = 0,  ///< horizontal, vertical or rotated: its line's angle is DimData::aux
    Aligned = 1, ///< parallel to the measured segment
    Radius = 2,
    Diameter = 3,
    Angular = 4,   ///< a = vertex, b = on ray 1, line_pt = on ray 2; aux = the arc's radius (v38)
    Ordinate = 5,  ///< a = feature, b = leader end; aux 0 = X datum, 1 = Y datum
    Jogged = 6,    ///< a = true centre, b = point on the arc, line_pt = centre override; aux = jog position 0..1
    ArcLength = 7, ///< a = centre, b = arc start point, line_pt = placement; aux = end angle (rad)
};

/// How a dimension qualifies its measured value.
///
/// The VALUE is always the measured def-point distance and can never be authored --
/// that is what makes a Musa CAD dimension trustworthy. Only the decoration AROUND
/// it is authorable, which is what a fabrication drawing actually needs (a fit class,
/// limit deviations, a bilateral tolerance, a basic-dimension box).
enum class TolMode : std::uint8_t {
    None = 0,      ///< the value alone
    Symmetric = 1, ///< value ± upper, on one line (bilateral)
    Limits = 2,    ///< value+upper stacked over value+lower (two lines)
    Basic = 3,     ///< the value in a box (ASME Y14.5 theoretically-exact dimension)
    Reference = 4, ///< the value in parentheses
    Deviation = 5, ///< the value, then +upper stacked over -lower beside it
};

/// A dimension's tolerance decoration. `lower` is the (usually negative) deviation;
/// Symmetric uses `upper` only.
struct DimTolerance {
    TolMode mode = TolMode::None;
    double upper = 0.0;
    double lower = 0.0;
    friend bool operator==(const DimTolerance&, const DimTolerance&) = default;
};

/// A dimension's authorable text decoration, as views into the store's char pool.
/// The strings are RAW (control codes unexpanded) -- expansion happens once, inside
/// compose_dim_label, at layout time (derived-not-baked). Declared here rather than
/// in dimension.hpp so both the store and the geometry builder can name it.
struct DimTextParts {
    std::string_view prefix;
    std::string_view suffix;
    /// The text override (issue #20). Empty = derive the label from the measurement.
    std::string_view text_override;
    /// DIMINSPECT's label and inspection rate (DimData::inspect says whether they show).
    std::string_view inspect_label{};
    std::string_view inspect_rate{};
    /// DIMBREAK's breaks, as pairs: two equal points are a break centred there (an object
    /// crossing; the gap is the style's break size), two different ones the stretch
    /// between them (Manual).
    std::span<const Vec2> breaks{};
};

/// Where a dimension's value is placed when it does not fit between the extension
/// lines (ISO 129-1 / ASME Y14.5). `Auto` measures the label and decides -- which is
/// the only correct default, because only the renderer knows the glyph advance.
/// `Inside` / `Outside` force the treatment for one dimension.
///
/// This governs the TEXT only. The arrowheads are resolved independently by their own
/// fit test, because the four combinations (both in, arrows out, text out, both out)
/// are all legitimate drawings -- collapsing them into one binary would put arrows
/// outside on a dimension whose arrows fit perfectly well.
enum class TextFit : std::uint8_t {
    Auto = 0,
    Inside = 1,
    Outside = 2,
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
    /// DIMTAD beyond Centered and Above: 2 Outside (on the side away from what is measured),
    /// 3 JIS, 4 Below; 0 leaves it to text_above. dim_text_vpos() reads the two together.
    std::uint8_t text_vpos = 0;
    /// DIMJUST, the Text tab's Horizontal: 0 Centered, 1 At Ext Line 1, 2 At Ext Line 2,
    /// 3 Over Ext Line 1, 4 Over Ext Line 2 (linear and aligned dimensions).
    std::uint8_t text_just = 0;
    std::uint8_t dim_lineweight = 25; ///< dimension/extension line weight (hundredths mm)
    std::uint8_t text_fit = 0;   ///< TextFit: narrow-dimension value placement (Auto)

    // Per-element colours (ByLayer by default, AutoCAD-style).
    ElementColor dim_color{};
    ElementColor ext_color{};
    ElementColor text_color{};
    ElementColor arrow_color{};

    // Primary units (v38): how the value is written.
    char decimal_separator = '.';      ///< DIMDSEP: '.' or ','
    std::uint8_t zero_suppression = 0; ///< DIMZIN's decimal bits: 4 leading zeros, 8 trailing
    /// DIMTMOVE: where moved text takes the dimension. 0 the dimension line moves with the
    /// text (it slides along the line, which extends under it beside the extension lines),
    /// 1 the text moves alone with a leader back to the line, 2 alone without one.
    std::uint8_t text_move = 0;

    // Units (v40). DIMLUNIT: 1 scientific, 2 decimal, 3 engineering, 4 architectural, 5
    // fractional (6, Windows desktop, reads as decimal); DIMFRAC: 0 horizontal, 1 diagonal,
    // 2 not stacked (fractions are written inline); DIMRND rounds to a multiple (0 none);
    // DIMLFAC multiplies every length measured; DIMAUNIT: 0 degrees, 1 degrees/minutes/
    // seconds, 2 grads, 3 radians, with DIMADEC places (-1: DIMDEC's).
    std::uint8_t linear_unit = 2;
    std::uint8_t fraction_format = 0;
    double round_off = 0.0;
    double linear_factor = 1.0;
    std::uint8_t angular_unit = 0;
    std::int8_t angular_precision = -1;
    // Alternate units (DIMALT): the length again times DIMALTF in DIMALTU with DIMALTD
    // places, rounded to DIMALTRND, in brackets after the value.
    bool alt = false;
    std::uint8_t alt_unit = 2;
    std::uint8_t alt_precision = 2;
    double alt_factor = 25.4;
    double alt_round = 0.0;
    /// DIMPOST: "<>" stands for the value ("<> mm"); text without it is a suffix. A prefix
    /// on a radius or diameter takes the place of R or the diameter sign.
    std::string post{};
    /// DIMAPOST: "[]" stands for the alternate value.
    std::string alt_post{};
    /// The Tolerances tab: the method a dimension without a tolerance of its own takes --
    /// 0 None, 1 Symmetrical, 2 Deviation, 3 Limits, 4 Basic (DIMTOL / DIMLIM / DIMGAP < 0) --
    /// the upper and lower values (DIMTP, DIMTM; the lower one entered positive, below the
    /// value) and their precision (DIMTDEC; -1: as the primary units').
    std::uint8_t tol_method = 0;
    double tol_upper = 0.0;
    double tol_lower = 0.0;
    std::int8_t tol_precision = -1;
    /// The Tolerances tab's Scaling for height (DIMTFAC): the tolerance's text height over
    /// the value's.
    double tol_height = 1.0;
    /// The Text tab's View Direction (DIMTXTDIRECTION): the text read right to left (true),
    /// its characters in the other order, or left to right.
    bool text_rtl = false;

    friend bool operator==(const DimStyle&, const DimStyle&) = default;
};

/// DIMZIN's two decimal bits.
inline constexpr std::uint8_t kDimZinLeading = 4;  ///< "0.50" is written ".50"
inline constexpr std::uint8_t kDimZinTrailing = 8; ///< "12.50" is written "12.5", "12.00" "12"
/// DIMZIN's feet-and-inches bits (Engineering, Architectural), AutoCAD's values: 0 both zero
/// feet and zero inches left out (6", 1'), 1 both kept (0'-6", 1'-0"), 2 zero feet kept
/// and zero inches left out, 3 zero inches kept and zero feet left out.
inline constexpr std::uint8_t kDimZinFeetInches = 3;

/// Per-dimension property overrides (AutoCAD's PR "this dimension only"). Mirrors
/// the ByLayer/override pattern: a bit in `mask` means "this field is overridden;
/// use the value here instead of the dimension's DimStyle". No bit => ByStyle. The
/// effective value is resolved once, in compute_dim_geometry (override-first). Kept
/// compact and only in the dimension's arena -- hot structs are untouched.
/// A multileader style (MLEADERSTYLE): what MLEADER draws a new multileader with -- the
/// arrowhead and its size, whether there is a landing and how long, the text height and
/// its gap from the landing, how many leader points, and whether it carries text.
/// Standard is the multileader Musa CAD drew before there were styles.
struct MLeaderStyle {
    std::string name = "Standard";
    std::uint8_t arrow = 0;          ///< ArrowType
    double arrow_size = 2.5;
    bool landing = true;             ///< Automatically include landing
    double landing_distance = 5.0;   ///< the landing's length
    double text_height = 2.5;
    double landing_gap = 1.25;       ///< between the landing and the text
    std::uint8_t max_points = 2;     ///< leader points before the text (0: no limit)
    bool text = true;                ///< Multileader type: MText (false: None)
    std::uint8_t leader_type = 0;    ///< Leader Format > Type: 0 Straight, 1 Spline, 2 None
    friend bool operator==(const MLeaderStyle&, const MLeaderStyle&) = default;
};

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
        kTextFit = 1u << 8,
        kTextMove = 1u << 9, ///< DIMTMOVE (the text grip's Move with Dim Line / Text Only / with Leader)
    };
    std::uint16_t mask = 0;
    std::uint8_t arrow_type = 0;
    std::uint8_t precision = 2;
    std::uint8_t text_fit = 0; ///< TextFit
    bool text_above = true;
    std::uint8_t text_move = 0; ///< DimStyle::text_move
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
/// A multileader's form (MLEADER's Leader type, LEADER's Format), as bits: a spline through
/// the points (the landing, the last segment when kLanding, left straight), no leader line
/// at all, no arrowhead.
struct LeaderForm {
    static constexpr std::uint8_t kSpline = 1u << 0;
    static constexpr std::uint8_t kNoLine = 1u << 1;
    static constexpr std::uint8_t kNoArrow = 1u << 2;
    static constexpr std::uint8_t kLanding = 1u << 3;
};

/// The Text tab's Vertical position, DIMTAD: 0 Centered, 1 Above, 2 Outside, 3 JIS, 4 Below.
[[nodiscard]] inline std::uint8_t dim_text_vpos(const DimStyle& s) noexcept {
    return s.text_vpos != 0 ? s.text_vpos : static_cast<std::uint8_t>(s.text_above ? 1 : 0);
}

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
        s.text_vpos = 0; // the dimension's own Above or Centered
    }
    if (o.has(DimOverrides::kDimColor)) {
        s.dim_color = ElementColor{false, o.dim_color};
        s.arrow_color = s.dim_color; // DIMCLRD colours the arrowheads too, as on a style
    }
    if (o.has(DimOverrides::kExtColor)) {
        s.ext_color = ElementColor{false, o.ext_color};
    }
    if (o.has(DimOverrides::kTextColor)) {
        s.text_color = ElementColor{false, o.text_color};
    }
    if (o.has(DimOverrides::kTextFit)) {
        s.text_fit = o.text_fit;
    }
    if (o.has(DimOverrides::kTextMove)) {
        s.text_move = o.text_move;
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
    /// ISOLATEOBJECTS / HIDEOBJECTS: hidden objects are neither drawn nor selectable
    /// until UNISOLATEOBJECTS. Bit 3, between the ByLayer bits and the space bits, so
    /// every record, capture and file form carries it unchanged.
    static constexpr std::uint8_t kHidden = 1u << 3;

    std::uint16_t layer = 0; ///< index into the store's layer table
    Rgb color{};             ///< override colour when !color_by_layer()
    Linetype linetype = Linetype::Continuous;
    std::uint8_t lineweight = 25;       ///< override, hundredths of a mm
    std::uint8_t flags = kAllByLayer;   ///< which properties are ByLayer

    /// The object's own transparency (the Properties palette's Transparency, CETRANSPARENCY):
    /// 0 to 90 percent, or ByLayer (its layer's) or ByBlock (the block reference's).
    static constexpr std::uint8_t kTransparencyByLayer = 255;
    static constexpr std::uint8_t kTransparencyByBlock = 254;
    std::uint8_t transparency = kTransparencyByLayer;
    /// ByBlock: the colour, linetype or lineweight of the block reference the object is drawn
    /// in (white, Continuous and the default lineweight outside one). A property set ByBlock
    /// is neither ByLayer nor an override of its own.
    static constexpr std::uint8_t kColorByBlock = 1u << 0;
    static constexpr std::uint8_t kLinetypeByBlock = 1u << 1;
    static constexpr std::uint8_t kLineweightByBlock = 1u << 2;
    std::uint8_t by_block = 0;
    /// Draw order (DRAWORDER): an object of a larger order is drawn over one of a smaller.
    /// 0 until the store gives the object the next one as it is added: the first is
    /// kFirstOrder + kOrderStep, with room below for Back and between two for Above / Under.
    static constexpr std::uint32_t kFirstOrder = 1u << 30;
    static constexpr std::uint32_t kOrderStep = 1024;
    std::uint32_t order = 0;

    [[nodiscard]] bool color_by_layer() const noexcept { return (flags & kColorByLayer) != 0; }
    [[nodiscard]] bool linetype_by_layer() const noexcept {
        return (flags & kLinetypeByLayer) != 0;
    }
    [[nodiscard]] bool lineweight_by_layer() const noexcept {
        return (flags & kLineweightByLayer) != 0;
    }
    /// ByLayer (true) or an override of its own (false); either way no longer ByBlock.
    void set_color_by_layer(bool v) noexcept {
        set_flag(kColorByLayer, v);
        set_by_block(kColorByBlock, false);
    }
    void set_linetype_by_layer(bool v) noexcept {
        set_flag(kLinetypeByLayer, v);
        set_by_block(kLinetypeByBlock, false);
    }
    void set_lineweight_by_layer(bool v) noexcept {
        set_flag(kLineweightByLayer, v);
        set_by_block(kLineweightByBlock, false);
    }
    [[nodiscard]] bool color_by_block() const noexcept { return (by_block & kColorByBlock) != 0; }
    [[nodiscard]] bool linetype_by_block() const noexcept { return (by_block & kLinetypeByBlock) != 0; }
    [[nodiscard]] bool lineweight_by_block() const noexcept { return (by_block & kLineweightByBlock) != 0; }
    /// ByBlock (true), or back to ByLayer (false).
    void set_color_by_block(bool v) noexcept {
        set_flag(kColorByLayer, !v);
        set_by_block(kColorByBlock, v);
    }
    void set_linetype_by_block(bool v) noexcept {
        set_flag(kLinetypeByLayer, !v);
        set_by_block(kLinetypeByBlock, v);
    }
    void set_lineweight_by_block(bool v) noexcept {
        set_flag(kLineweightByLayer, !v);
        set_by_block(kLineweightByBlock, v);
    }
    [[nodiscard]] bool hidden() const noexcept { return (flags & kHidden) != 0; }
    void set_hidden(bool v) noexcept { set_flag(kHidden, v); }

    /// The space the entity lives in: 0 = model space, otherwise a layout's id (paper
    /// space). Carried in the upper bits of `flags`, so every record, command, capture
    /// and file form already moves it around unchanged.
    static constexpr std::uint8_t kSpaceShift = 4;
    [[nodiscard]] std::uint8_t space() const noexcept {
        return static_cast<std::uint8_t>(flags >> kSpaceShift);
    }
    void set_space(std::uint8_t s) noexcept {
        flags = static_cast<std::uint8_t>((flags & 0x0Fu) | ((s & 0x0Fu) << kSpaceShift));
    }

    /// The same properties. The draw order is where an object is drawn, not what it is drawn
    /// with: two objects alike are equal whichever is above.
    friend bool operator==(const EntityProps& a, const EntityProps& b) noexcept {
        return a.layer == b.layer && a.color == b.color && a.linetype == b.linetype && a.lineweight == b.lineweight &&
               a.flags == b.flags && a.transparency == b.transparency && a.by_block == b.by_block;
    }

private:
    void set_flag(std::uint8_t bit, bool v) noexcept {
        if (v) {
            flags = static_cast<std::uint8_t>(flags | bit);
        } else {
            flags = static_cast<std::uint8_t>(flags & ~bit);
        }
    }
    void set_by_block(std::uint8_t bit, bool v) noexcept {
        by_block = static_cast<std::uint8_t>(v ? (by_block | bit) : (by_block & ~bit));
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
    // ByBlock outside a block reference: white, Continuous, the default lineweight (in one,
    // the reference's -- block_resolve's member_props).
    if (e.color_by_block()) {
        out.color = Rgb{255, 255, 255};
    }
    if (e.linetype_by_block()) {
        out.linetype = Linetype::Continuous;
    }
    if (e.lineweight_by_block()) {
        out.lineweight = 25;
    }
    return out;
}

} // namespace musacad::core
