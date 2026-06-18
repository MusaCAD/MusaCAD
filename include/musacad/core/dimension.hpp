// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <string>
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
    std::vector<Vec2> dim_lines;   ///< dimension line(s) / arc
    std::vector<Vec2> arrow_lines; ///< open/tick arrowheads (segments)
    std::vector<Vec2> arrow_fills; ///< filled arrowheads (triangles, 3 Vec2 each)

    Rgb ext_color;
    Rgb dim_color;
    Rgb arrow_color;
    Rgb text_color;
    std::uint8_t lineweight = 25;

    std::string label;
    Vec2 text_pos{};
    double text_rotation = 0.0;
    double text_height = 2.5;
    text::Justify text_justify = text::Justify::Center;
};

/// The measured value of a dimension (computed from def points -- never baked).
[[nodiscard]] double dim_measure(const DimData& d);

/// Formats a measurement with `precision` decimal places.
[[nodiscard]] std::string format_measurement(double value, std::uint8_t precision);

/// Computes a dimension's drawable geometry under a style. `base_color` is the
/// entity's ByLayer-resolved colour, used for any element whose style colour is
/// ByLayer. Linear/Aligned/Radius/Diameter/Angular are built.
[[nodiscard]] DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style,
                                               Rgb base_color);

/// Appends a filled (triangles) or stroked (segments) arrowhead at `tip` pointing
/// back along `along` (unit), sized to `size`, of the given ArrowType. Shared with
/// leaders.
void append_arrowhead(std::vector<Vec2>& fills, std::vector<Vec2>& lines, Vec2 tip, Vec2 along,
                      double size, ArrowType type);

} // namespace musacad::core
