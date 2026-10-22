// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core {

/// What a spline drawn by fit points (SPLINE's Fit method) was fitted with, kept on it as
/// AutoCAD keeps a spline's fit data: its grips are on the points, and it is fitted again
/// from them (spline::fit_spline) when one moves. Empty: a spline by control vertices, or one
/// whose fit data an edit of its control vertices dropped.
struct SplineFit {
    std::vector<Vec2> points;          ///< the points it passes through (closed: the first not repeated)
    std::uint8_t knots = 0;            ///< spline::FitParam: 0 Chord, 1 Square root, 2 Uniform
    double tolerance = 0.0;            ///< 0: through the points; above, within that of them
    bool closed = false;               ///< round to the first point again, smooth across it
    std::optional<Vec2> start_tangent{};
    std::optional<Vec2> end_tangent{};
    [[nodiscard]] bool empty() const noexcept { return points.empty(); }
    friend bool operator==(const SplineFit&, const SplineFit&) = default;
};

} // namespace musacad::core
