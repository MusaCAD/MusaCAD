// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran
//
// What MIRROR does to text (AutoCAD's MIRRTEXT): with MIRRTEXT 0 -- the default -- a
// mirrored text keeps reading the right way round: its insertion point is reflected and
// its rotation reflected too, and when that leaves the reading direction pointing
// backwards the text is turned round and its justification swapped, so it sits in the
// mirrored place without becoming a mirror image. With MIRRTEXT 1 the text is reflected
// like any other object (here: the reflected rotation, which reads back to front).
#pragma once

#include <cmath>
#include <cstdint>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// The rotation a mirrored text gets. `axis` is the mirror line's angle (radians);
/// `justify` (text::TextJustify) changes sides with the text: left and right when it is
/// turned round, top and bottom when it is not (the box justifications only).
[[nodiscard]] inline double mirrored_text_rotation(double rotation, double axis, bool mirrtext,
                                                   std::uint8_t& justify) noexcept {
    double r = 2.0 * axis - rotation;
    if (mirrtext) {
        return r;
    }
    const double c = std::cos(r);
    const double s = std::sin(r);
    // Reading direction pointing left (or straight down): turn round, swap the ends.
    const bool turned = c < -1e-9 || (std::abs(c) <= 1e-9 && s < 0.0);
    if (turned) {
        r += kPi;
    }
    if (justify <= 2) {
        if (turned && justify != 1) {
            justify = static_cast<std::uint8_t>(2 - justify);
        }
    } else if (justify >= 6 && justify <= 14) {
        // A corner or an edge of the box (TL .. BR, three to a row): turned round, the
        // text's left is its right; not turned, its top is its bottom -- either way it
        // fills the mirrored box and reads the right way.
        std::uint8_t col = static_cast<std::uint8_t>((justify - 6) % 3);
        std::uint8_t row = static_cast<std::uint8_t>((justify - 6) / 3);
        if (turned) {
            col = static_cast<std::uint8_t>(2 - col);
        } else {
            row = static_cast<std::uint8_t>(2 - row);
        }
        justify = static_cast<std::uint8_t>(6 + row * 3 + col);
    }
    return r;
}

/// The same for an MTEXT attachment (0..8 = TL,TC,TR,ML,MC,MR,BL,BC,BR): the column
/// swaps sides when the text is turned round.
[[nodiscard]] inline double mirrored_mtext_rotation(double rotation, double axis, bool mirrtext,
                                                    std::uint8_t& attach) noexcept {
    std::uint8_t col = static_cast<std::uint8_t>(attach % 3);
    const std::uint8_t row = static_cast<std::uint8_t>(attach / 3);
    const double r = mirrored_text_rotation(rotation, axis, mirrtext, col);
    attach = static_cast<std::uint8_t>(row * 3 + col);
    return r;
}

} // namespace musacad::core
