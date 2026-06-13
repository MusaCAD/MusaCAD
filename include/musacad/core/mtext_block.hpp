// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <cstdint>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// The discrete, queryable formatting fields of a paragraph text block (MTEXT and
/// the QLEADER label both use one). Layout/glyphs are COMPUTED from these at
/// snapshot time (text/mtext.cpp) -- never baked -- so the future Properties
/// palette can edit any field. Content lives in the shared string pool
/// (`str_offset`/`str_len`), as single-line TEXT does.
struct MTextBlock {
    Vec2 pos;                  ///< attachment/insertion point
    double width = 0.0;        ///< wrap width (0 = no wrap; only explicit \n breaks)
    double height = 2.5;       ///< cap height per line
    double rotation = 0.0;     ///< radians, CCW
    double width_factor = 1.0; ///< horizontal glyph scale
    double line_spacing = 1.0; ///< multiple of the single-line height
    std::uint8_t attach = 0;   ///< 0..8 = TL,TC,TR,ML,MC,MR,BL,BC,BR
    std::uint16_t font = 0;    ///< index into the store's font table (0 = Standard/stroke)
    std::uint32_t str_offset = 0;
    std::uint32_t str_len = 0;
    friend bool operator==(const MTextBlock&, const MTextBlock&) = default;
};

} // namespace musacad::core
