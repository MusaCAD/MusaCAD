#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "musacad/core/geometry_store.hpp"
#include "musacad/core/math/math.hpp"
#include "musacad/core/text/stroke_font.hpp"

namespace musacad::core {

/// The resolved, drawable geometry of a dimension, computed from its definition
/// points + style. Lines/arrows are flat segment-pair lists (two Vec2 per
/// segment) so they batch with everything else. The text is returned as a string
/// + placement for the caller to lay out with the stroke font.
struct DimGeometry {
    std::vector<Vec2> lines;  ///< extension lines + the dimension line
    std::vector<Vec2> arrows; ///< arrowhead segments (filled-triangle fan or tick)
    std::string label;        ///< measured text (e.g. "42.50")
    Vec2 text_pos{};          ///< baseline anchor for the label
    double text_rotation = 0.0;
    double text_height = 2.5;
    text::Justify text_justify = text::Justify::Center;
};

/// The measured value of a dimension (computed from def points -- never baked).
[[nodiscard]] double dim_measure(const DimData& d);

/// Formats a measurement with `precision` decimal places.
[[nodiscard]] std::string format_measurement(double value, std::uint8_t precision);

/// Computes the full drawable geometry of a dimension under a style. Linear and
/// Aligned are fully built; other types produce a best-effort subset (see
/// COMMANDS.md for what's Implemented vs Partial).
[[nodiscard]] DimGeometry compute_dim_geometry(const DimData& d, const DimStyle& style);

} // namespace musacad::core
