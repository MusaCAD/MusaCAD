// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <vector>

#include "musacad/core/geometry_kernel.hpp"

namespace musacad::core {

/// The default, fully-functional 2D geometry kernel. Operates directly on the
/// cache-friendly GeometryStore and covers every 2D primitive (point, line,
/// polyline, circle, arc, spline).
class NativeKernel2D final : public IGeometryKernel {
public:
    void tessellate(const GeometryStore& store, EntityHandle entity, double tolerance,
                    std::vector<Vec2>& out) const override;

    bool closest_point(const GeometryStore& store, EntityHandle entity, Vec2 query,
                       Vec2& out_point) const override;

    void intersect(const GeometryStore& store, EntityHandle a, EntityHandle b,
                   std::vector<Vec2>& out) const override;

    bool offset(const GeometryStore& store, EntityHandle entity, double distance, Vec2 side,
                Command& out) const override;

    // --- Shared analytic primitives (used by Extend/Trim/Fillet/Chamfer) ---

    /// Intersection of two *infinite* lines a0a1 and b0b1. Returns false when
    /// (near-)parallel. Exact -- the basis for corner construction.
    [[nodiscard]] static bool line_line_intersection(Vec2 a0, Vec2 a1, Vec2 b0, Vec2 b1, Vec2& out);

    /// Intersection of the *infinite* line through a,b with a circle. Returns the
    /// number of hits (0/1/2), filling p0 (and p1 for 2).
    [[nodiscard]] static int line_circle_intersection(Vec2 a, Vec2 b, Vec2 center, double radius,
                                                       Vec2& p0, Vec2& p1);

    /// Intersection of two circles. Returns the number of hits (0 = separate / one inside
    /// the other / concentric, 1 = tangent, 2), filling p0 (and p1 for 2). Used to re-miter
    /// arc/arc corners when offsetting a bulged polyline.
    [[nodiscard]] static int circle_circle_intersection(Vec2 c0, double r0, Vec2 c1, double r1,
                                                        Vec2& p0, Vec2& p1);
};

} // namespace musacad::core
