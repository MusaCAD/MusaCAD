#pragma once

#include <vector>

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

} // namespace musacad::render
