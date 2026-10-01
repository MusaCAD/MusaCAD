// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string_view>

#include "musacad/core/math/math.hpp"
#include "musacad/core/text/justify.hpp"

namespace musacad::core {
class GeometryStore;
struct TextData;
} // namespace musacad::core

namespace musacad::core::text {

/// A single-line text as it is laid out: the ONE place its justification, its style and
/// its font's measure come together, so drawing, bounds, picking, grips and the edit box
/// cannot disagree about where the text is.
struct TextFrame {
    Vec2 origin{};            ///< the left end of the baseline
    double height = 1.0;      ///< as drawn (Aligned scales it)
    double rotation = 0.0;    ///< as drawn (Aligned and Fit take it from their points)
    double width_factor = 1.0; ///< the style's, the text's own and Fit's, multiplied
    double oblique = 0.0;
    double advance = 0.0;     ///< the run's advance at `height`, before the width factor
    [[nodiscard]] double width() const noexcept { return advance * width_factor; }
    [[nodiscard]] JustifyFrame justify_frame() const noexcept {
        return {origin, height, rotation, width_factor};
    }
};

/// The frame of a TEXT or of an ATTDEF's tag in the store.
[[nodiscard]] TextFrame frame_of(const GeometryStore& store, const TextData& t);

/// The same from values, measured with the stroke font: a block's member text, a text in
/// a file being written.
[[nodiscard]] TextFrame frame_of(std::string_view content, std::uint8_t justify, Vec2 pos, Vec2 align,
                                 double height, double rotation, double width_factor);

} // namespace musacad::core::text
