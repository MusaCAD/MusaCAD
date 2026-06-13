// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::render {

using core::Vec2;

/// Appends screen-space line segments (two Vec2 per segment, y-down pixels) that
/// draw `text` with its top-left at `origin` and glyph height `height_px`.
/// Returns the total advance width in pixels. A deliberately tiny stroke font
/// for the debug overlay: digits, '.', ' ', and the letters F P S M.
double append_text_segments(std::string_view text, Vec2 origin, double height_px,
                            std::vector<Vec2>& out);

} // namespace musacad::render
