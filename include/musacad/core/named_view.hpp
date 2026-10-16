// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <string>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// A named view (AutoCAD VIEW): a camera centre and scale (pixels per drawing unit)
/// saved under a name, restored later. Stored with the drawing.
struct NamedView {
    std::string name;
    Vec2 center{};
    double scale = 1.0;
    /// The layer snapshot saved with the view (a layer state, "*VIEW|" + the name), put
    /// back when the view is restored; empty: none.
    std::string layer_state{};
    friend bool operator==(const NamedView&, const NamedView&) = default;
};

} // namespace musacad::core
