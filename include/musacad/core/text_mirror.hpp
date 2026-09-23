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
/// `justify` (0 left, 1 centre, 2 right) is swapped when the text is turned round.
[[nodiscard]] inline double mirrored_text_rotation(double rotation, double axis, bool mirrtext,
                                                   std::uint8_t& justify) noexcept {
    double r = 2.0 * axis - rotation;
    if (mirrtext) {
        return r;
    }
    const double c = std::cos(r);
    const double s = std::sin(r);
    // Reading direction pointing left (or straight down): turn round, swap the ends.
    if (c < -1e-9 || (std::abs(c) <= 1e-9 && s < 0.0)) {
        r += kPi;
        if (justify == 0) {
            justify = 2;
        } else if (justify == 2) {
            justify = 0;
        }
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
