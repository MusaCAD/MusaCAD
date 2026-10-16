// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/text/justify.hpp"

namespace musacad::core {
class IFontEngine;
struct TextStyle;
} // namespace musacad::core

namespace musacad::core::text {

/// The drawn geometry of one line of text, as the drawing shows it: control codes expanded,
/// justified along the baseline, the style's width factor and obliquing applied, mirrored
/// for Backwards and Upside down. An outline (TTF/OTF) face gives `tris` (three Vec2 each)
/// or, when asked, `rings` (its closed outlines); the stroke font gives `segments` (two Vec2
/// each). The over- and underline bars are `bars` (two Vec2 each) either way.
struct TextRunGeometry {
    bool outline = false;
    std::vector<Vec2> segments;
    std::vector<Vec2> tris;
    std::vector<std::vector<Vec2>> rings;
    std::vector<Vec2> bars;
};

/// Fills `out` (cleared first) for `raw` laid out from `origin` at cap `height`, `rotation`
/// radians, in the font `font_name` (`fonts` null or the name not an outline face: the
/// stroke font). `ts` gives the width factor, obliquing and the Backwards / Upside down
/// flags (null: none). `outlines` asks an outline face for `rings` instead of `tris`.
/// The one path for the drawing, the plot and TXTEXP.
void text_run_geometry(std::string_view raw, Vec2 origin, double height, double rotation, Justify justify,
                       std::string_view font_name, const IFontEngine* fonts, const TextStyle* ts, bool outlines,
                       TextRunGeometry& out);

} // namespace musacad::core::text
