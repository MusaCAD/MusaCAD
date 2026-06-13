// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Kiran Pranay

#pragma once

#include <span>
#include <vector>

#include "musacad/core/math/math.hpp"
#include "musacad/core/properties.hpp"

// Linetype dash-pattern rendering. Dashing is DERIVED at snapshot time from the
// stored linetype + the global LTSCALE -- nothing is baked into storage (same
// parametric rule as tessellation). A single walker dashes any polyline by
// arc-length, so a straight line (2 points) and a tessellated curve (N points)
// share one path and the pattern phase carries across vertices (no restart /
// bunching at tessellation joins).

namespace musacad::core {

/// The base dash pattern for a linetype, in drawing units: even indices are "on"
/// (dash) lengths, odd indices are "off" (gap) lengths. Empty span => Continuous
/// (no dashing). Multiply by LTSCALE before use. Proportions follow AutoCAD's
/// acad.lin (x10 so they read at LTSCALE 1 in a tens-of-units drawing):
///   Dashed  = [5, 2.5]                 (acad .5,-.25)
///   Hidden  = [2.5, 1.25]              (acad .25,-.125)
///   Center  = [12.5, 2.5, 2.5, 2.5]    (acad 1.25,-.25,.25,-.25: long,gap,dot,gap)
[[nodiscard]] std::span<const double> dash_pattern(Linetype lt) noexcept;

/// Append dash sub-segment endpoint pairs (a0,b0, a1,b1, ...) to `out`, walking the
/// polyline `pts` by arc-length with `lt`'s pattern scaled by `ltscale`. Continuous
/// (or ltscale <= 0, or a <2-point input) copies every segment unchanged. The phase
/// starts pen-down at the first point and carries continuously across all segments.
void dash_polyline(std::span<const Vec2> pts, Linetype lt, double ltscale,
                   std::vector<Vec2>& out);

} // namespace musacad::core
