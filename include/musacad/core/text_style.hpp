// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>

namespace musacad::core {

/// A named text style (AutoCAD STYLE): the font, a fixed height (0 = ask at TEXT time),
/// a width factor and an obliquing angle. Style 0 is "Standard" (the stroke font).
/// Single-line TEXT references a style; the geometry is derived at draw time, so
/// editing a style re-lays-out every text that uses it.
struct TextStyle {
    std::string name = "Standard";
    std::string font;           ///< font name ("" = the stroke font)
    double height = 0.0;        ///< 0 = not fixed
    double width_factor = 1.0;  ///< horizontal scale
    double oblique = 0.0;       ///< radians, positive slants to the right
    bool backwards = false;     ///< mirrored left to right about the insertion point (DXF 71 bit 2)
    bool upside_down = false;   ///< mirrored about the baseline (DXF 71 bit 4)
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

} // namespace musacad::core
