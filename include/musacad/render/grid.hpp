// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <vector>

#include "musacad/core/grid_settings.hpp"
#include "musacad/render/camera.hpp"

namespace musacad::render {

/// A screen-adaptive world-space grid. Spacing follows decade stepping (powers
/// of ten) so the minor spacing stays in a comfortable on-screen pixel range as
/// the user zooms; major lines fall every tenth minor line.
struct GridResult {
    std::vector<Vec2> minor; ///< line endpoints (2 per segment), world space
    std::vector<Vec2> major; ///< line endpoints (2 per segment), world space
    double minor_spacing = 1.0;
    double major_spacing = 10.0;
};

/// Chooses the decade minor spacing (world units) such that one minor cell is at
/// least `target_minor_px` pixels on screen at the given scale.
[[nodiscard]] double choose_minor_spacing(double scale, double target_minor_px) noexcept;

/// Builds the visible grid for `camera`. `max_lines_per_axis` caps generation as
/// a safety valve against pathological inputs.
[[nodiscard]] GridResult build_grid(const Camera2D& camera, double target_minor_px = 14.0,
                                    int max_lines_per_axis = 4096);

/// The grid GRID set up: lines `settings.x` / `settings.y` apart (a spacing of 0 is the
/// zoom-following grid above), a major one every `settings.major`; adaptive, a spacing
/// too fine on screen is multiplied by `major` until it is not, and with `subdivide` one
/// far apart is divided by it. Without `beyond_limits` the grid stops at `lim_lo` /
/// `lim_hi`. Too fine and not adaptive: no grid.
[[nodiscard]] GridResult build_grid(const Camera2D& camera, const core::GridSettings& settings, Vec2 lim_lo,
                                    Vec2 lim_hi, int max_lines_per_axis = 4096);

} // namespace musacad::render
