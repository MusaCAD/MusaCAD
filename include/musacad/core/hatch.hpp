// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Pranay Kiran

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "musacad/core/math/math.hpp"

namespace musacad::core::hatch {

/// The gradient fills (#41): AutoCAD's nine, named as its gallery names them (GR_LINEAR,
/// GR_CYLIN ...). GRADIENT, the name drawings from before have, is GR_LINEAR.
enum class GradientShape : std::uint8_t {
    Linear,
    Cylinder,
    InvCylinder,
    Spherical,
    InvSpherical,
    Hemispherical,
    InvHemispherical,
    Curved,
    InvCurved,
};
inline constexpr GradientShape kGradientShapes[] = {
    GradientShape::Linear,       GradientShape::Cylinder,      GradientShape::InvCylinder,
    GradientShape::Spherical,    GradientShape::InvSpherical,  GradientShape::Hemispherical,
    GradientShape::InvHemispherical, GradientShape::Curved,    GradientShape::InvCurved,
};
/// The gradient a pattern name is, or nothing for any other pattern.
[[nodiscard]] std::optional<GradientShape> gradient_shape(std::string_view pattern) noexcept;
[[nodiscard]] inline bool is_gradient(std::string_view pattern) noexcept { return gradient_shape(pattern).has_value(); }
/// A shape's pattern name (GR_LINEAR ...) and its name in DXF (470: LINEAR, CYLINDER ...).
[[nodiscard]] std::string_view gradient_pattern_name(GradientShape s) noexcept;
[[nodiscard]] std::string_view gradient_dxf_name(GradientShape s) noexcept;
/// The shape read from a DXF name, LINEAR when it is not one.
[[nodiscard]] GradientShape gradient_from_dxf(std::string_view name) noexcept;
/// How far a point is from the first colour (0) to the second (1), where (u, v) is where it
/// is in the fill's extents, 0 to 1 along the gradient's angle and across it. Centred, as
/// AutoCAD draws them by default: a cylinder lightest down the middle, a sphere at the
/// centre, a hemisphere from the middle of the lower edge.
[[nodiscard]] double gradient_value(GradientShape s, double u, double v) noexcept;

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
