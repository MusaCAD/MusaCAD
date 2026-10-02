// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"
#include "musacad/core/text/stroke_font.hpp"

namespace musacad::core {

/// The resolved, drawable geometry of a dimension, computed from its definition
/// points + style. Segment lists (two Vec2 per segment) and arrow fills
/// (three Vec2 per triangle) carry their own resolved colour so the snapshot can
/// route each into the right colour batch. Text is a string + placement for the
/// caller to lay out with the stroke font.
struct DimGeometry {
    std::vector<Vec2> ext_lines;   ///< extension lines
    std::vector<Vec2> dim_lines;   ///< dimension line(s) / arc (+ the basic-dimension box)
    std::vector<Vec2> arrow_lines; ///< open/tick arrowheads (segments)
    std::vector<Vec2> arrow_fills; ///< filled arrowheads (triangles, 3 Vec2 each)

    Rgb ext_color;
    Rgb dim_color;
    Rgb arrow_color;
    Rgb text_color;
    std::uint8_t lineweight = 25;

    std::string label;   ///< first (usually only) label line, codes already expanded
    Vec2 text_pos{};
    /// Second label line -- ONLY the `Limits` tolerance mode produces one, where the
    /// upper limit stacks over the lower. Empty otherwise, so every consumer can draw
    /// it unconditionally with `if (!label2.empty())`.
    std::string label2;
    Vec2 label2_pos{};
    double text_rotation = 0.0;
    double text_height = 2.5;
    text::Justify text_justify = text::Justify::Center;

    /// Where the label WOULD sit with no author displacement (issue #21). Meaningful
    /// only when `text_moved`; it is where the text grip returns to on "home text", and
    /// where the connector leader starts from.
    Vec2 derived_text_pos{};
    bool text_moved = false;
};

/// The composed, code-expanded label of a dimension: one line, or two when the
/// tolerance mode is `Limits`. `boxed` requests the ASME Y14.5 basic-dimension frame.
struct DimLabel {
    std::string line1;
    std::string line2; ///< empty unless TolMode::Limits
    bool boxed = false;
};

/// Builds a dimension's visible label from its MEASURED value plus its decoration.
/// The single definition -- compute_dim_geometry calls it to lay the text out, and
/// the DXF exporter calls it to fill the DIMENSION text override (group code 1), so
/// what another CAD shows can never disagree with what Musa CAD draws.
///
/// `style` must already have the dimension's overrides applied (it is read only for
/// `precision`). `parts` are the raw prefix/suffix; control codes are expanded here.
[[nodiscard]] DimLabel compose_dim_label(const DimData& d, const DimStyle& style,
                                         DimTextParts parts);

/// The four world-space corners of a placed label line, in order (baseline-left,
/// baseline-right, cap-right, cap-left), honouring justification and rotation.
/// `second` selects the Limits mode's lower line. Returns false when that line is
/// empty. THE definition of where a dimension's text sits: bounds, pick and the
/// ISO 129-1 fit test all read it, so none of them can disagree with what is drawn.
[[nodiscard]] bool dim_label_quad(const DimGeometry& g, bool second, Vec2 (&out)[4]);

/// The measured value of a dimension (computed from def points -- never baked).
[[nodiscard]] double dim_measure(const DimData& d);

/// The unit direction a linear or aligned dimension measures along: an aligned one from
/// `a` to `b`; a linear one at its line's angle (DimData::aux), turned to read left to
/// right or upwards. A linear dimension from before v38 (aux 0) measures along the axis
/// its points differ most on, as it always did.
[[nodiscard]] Vec2 dim_line_direction(const DimData& d);

/// DIMLINEAR's automatic orientation, AutoCAD's: a dimension line placed above or below
/// what is measured -- the box with corners `a` and `b` -- measures across it (0,
/// horizontal); one placed to its left or right measures up it (pi/2, vertical). Off a
/// corner, the side `line_pt` stands further out on decides; inside the box, its longer
/// side is measured.
[[nodiscard]] double linear_dim_auto_angle(Vec2 a, Vec2 b, Vec2 line_pt);

/// The DimData::aux a linear dimension whose line runs at `angle` (radians) keeps: the
/// angle taken into (0, pi], so a horizontal dimension is pi (0 means "from before v38").
[[nodiscard]] double linear_dim_aux(double angle);

/// What an angular dimension was made from, for AutoCAD's placement rule.
enum class AngularFrom : std::uint8_t {
    Lines,  ///< two lines: the four angles their rays make
    Arc,    ///< an arc: its own angle (b its start, line_pt its end, counter-clockwise)
    Points, ///< three points or a circle: from the first endpoint to the second, or the rest
};

/// Puts an angular dimension's arc through `at` (`Specify dimension arc line location`):
/// its radius, and -- from `quadrant` when given (the Quadrant option), else from `at` --
/// the angle it stands in: for two lines the one of their four between the rays around
/// it (a ray turned through the vertex keeps its point's distance), for three points the
/// angle from the first endpoint to the second or the rest of the turn. Sets `d.aux`.
void place_angular_dim(DimData& d, Vec2 at, AngularFrom from, std::optional<Vec2> quadrant = std::nullopt);

/// The counter-clockwise turn from the direction of `from` to that of `to`, in (0, 2 pi].
[[nodiscard]] double ccw_sweep(Vec2 from, Vec2 to);

/// Sets a linear dimension's angle for a dimension line through `d.line_pt`: `fixed`
/// when the author chose one (Horizontal, Vertical, Rotated), else the automatic one.
/// `circle`: `a` and `b` are the ends of a circle's horizontal diameter (DIMLINEAR of a
/// circle) -- the box is the circle's, and they turn to the diameter the angle measures.
void orient_linear_dim(DimData& d, std::optional<double> fixed, bool circle = false);

/// Formats a measurement with `precision` decimal places.
[[nodiscard]] std::string format_measurement(double value, std::uint8_t precision);

/// Formats a dimension's value as its style writes it: the style's precision, its
/// decimal separator, and its zero suppression (leading, trailing).
[[nodiscard]] std::string format_dim_value(double value, const DimStyle& style);

/// The dimension variables a style answers to, AutoCAD's names: DIMTXT (text height),
/// DIMASZ (arrow size), DIMBLK (arrowhead: 0 filled, 1 tick, 2 open, 3 dot), DIMDEC
/// (decimal places, 0..8), DIMDSEP (the separator's character code, '.' or ','),
/// DIMZIN (4 leading, 8 trailing zeros suppressed), DIMEXO / DIMEXE (extension line
/// offset and extension), DIMTAD (1 text above the line, 0 centred), DIMATFIT here as
/// the text fit (0 auto, 1 inside, 2 outside), DIMLWD (lineweight, 1/100 mm), DIMCLRD /
/// DIMCLRE / DIMCLRT (dimension line and arrowheads, extension lines, text: an ACI
/// colour, 256 or 0 ByLayer).
inline constexpr const char* kDimVars[] = {"DIMTXT", "DIMASZ", "DIMBLK", "DIMDEC", "DIMDSEP", "DIMZIN",
                                           "DIMEXO", "DIMEXE", "DIMTAD", "DIMATFIT", "DIMLWD", "DIMCLRD",
                                           "DIMCLRE", "DIMCLRT"};

/// Sets `var` (one of kDimVars, any case) on `style`; false for an unknown variable or a
/// value out of its range, `style` then unchanged.
bool apply_dim_var(DimStyle& style, std::string_view var, double value);

/// The value `var` has in `style` (a colour as its ACI, 256 = ByLayer, -1 = a true colour
/// with no ACI); nullopt for an unknown variable.
[[nodiscard]] std::optional<double> dim_var_value(const DimStyle& style, std::string_view var);

/// Computes a dimension's drawable geometry under a style. `base_color` is the
/// entity's ByLayer-resolved colour, used for any element whose style colour is
/// ByLayer. Linear/Aligned/Radius/Diameter/Angular are built.
///
/// `parts` carries the dimension's prefix/suffix, which live in the store's char pool
/// and so cannot be read from `DimData` alone -- callers that have the store pass
/// `store.dim_text_parts(d)`; the placement preview (which is decorating nothing yet)
/// passes the default. Decoration is resolved HERE so it participates in the
/// dimension's own layout, bounds and selection rather than floating beside it.
[[nodiscard]] DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style,
                                               Rgb base_color, DimTextParts parts = {});

/// Appends a filled (triangles) or stroked (segments) arrowhead at `tip` pointing
/// back along `along` (unit), sized to `size`, of the given ArrowType. Shared with
/// leaders.
void append_arrowhead(std::vector<Vec2>& fills, std::vector<Vec2>& lines, Vec2 tip, Vec2 along,
                      double size, ArrowType type);

} // namespace musacad::core
