// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <optional>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::hatch {

/// One candidate boundary edge (a straight segment; arcs/circles are pre-tessellated).
struct Segment {
    Vec2 a;
    Vec2 b;
};

/// Triangulate the FILLED region of a set of closed loops (loop 0 = outer, the rest
/// islands/holes) into triangles -- three Vec2 per triangle, appended to `out`. Uses an
/// exact trapezoidal decomposition with the even-odd rule, so holes drop out naturally
/// (a point covered by an even number of loops is empty) with no ear-clipping or bridging.
/// Slanted edges are followed exactly (trapezoids, not stair-stepped horizontal strips).
void triangulate_filled(const std::vector<std::vector<Vec2>>& loops, std::vector<Vec2>& out);

/// True if `p` lies in the filled region (even-odd ray cast over every loop). Used by the
/// pick test so a click anywhere inside the hatch (but outside its islands) selects it.
[[nodiscard]] bool point_in_loops(const std::vector<std::vector<Vec2>>& loops, Vec2 p);

/// Pick-point boundary detection: trace the smallest closed loop enclosing `p` from a set of
/// candidate boundary segments (lines, polyline segments, tessellated arcs/circles). The
/// segments are first split at every crossing / T-junction into a planar arrangement, so a
/// chord that PARTITIONS a region (its endpoints landing mid-edge) correctly subdivides the
/// face -- a pick in one partition traces just that sub-region. Endpoints are snapped within
/// `tol` (basic gap bridging). Returns the loop's vertices, or nullopt if no closed boundary
/// encloses `p`.
[[nodiscard]] std::optional<std::vector<Vec2>> trace_boundary(const std::vector<Segment>& segs,
                                                              Vec2 p, double tol);

} // namespace musacad::core::hatch
