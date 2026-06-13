// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::render {

/// Transient, render-side interaction visuals: the live command preview
/// (rubber-banding), the selection rubber-band rectangle, and the move/mirror
/// "ghost" transform applied to the currently-selected geometry. None of this is
/// real geometry -- it is composed on the UI thread from the active command's
/// state + the live cursor and handed to the renderer like the camera/crosshair,
/// with no geometry-thread round-trip.
struct RenderOverlay {
    /// Preview line segments (2 points per segment), world space.
    std::vector<core::Vec2> preview_segments;

    /// Selection rubber-band: 0 = none, 1 = window (blue), 2 = crossing (green).
    int rect_mode = 0;
    core::Vec2 rect_a{};
    core::Vec2 rect_b{};

    /// Ghost transform applied by the renderer to the snapshot's selected
    /// geometry: 0 = none, 1 = move (translate by ghost_b - ghost_a),
    /// 2 = mirror (reflect across ghost_a..ghost_b), 3 = rotate about ghost_a by
    /// ghost_param radians, 4 = scale about ghost_a by ghost_param.
    int ghost_mode = 0;
    core::Vec2 ghost_a{};
    core::Vec2 ghost_b{};
    double ghost_param = 0.0;

    void clear() noexcept {
        preview_segments.clear();
        rect_mode = 0;
        ghost_mode = 0;
    }
};

} // namespace musacad::render
